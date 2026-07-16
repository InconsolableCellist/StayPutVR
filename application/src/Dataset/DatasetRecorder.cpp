#include "DatasetRecorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "../../../common/Logger.hpp"
#include "../../../common/PathUtils.hpp"
#include "../../../common/Version.hpp"
#include "git_hash.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace StayPutVR {

    namespace {
        double NowWall() {
            return std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        double NowMono() {
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::string LocalTimestampForDirName() {
            std::time_t t = std::time(nullptr);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &t);
#else
            localtime_r(&t, &tm_buf);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_buf);
            return buf;
        }

        std::string WallToIso(double wall_seconds) {
            std::time_t t = static_cast<std::time_t>(wall_seconds);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &t);
#else
            localtime_r(&t, &tm_buf);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
            return buf;
        }

        const char* DeviceTypeName(uint8_t type) {
            switch (static_cast<DeviceType>(type)) {
                case DeviceType::HMD: return "HMD";
                case DeviceType::CONTROLLER: return "Controller";
                case DeviceType::TRACKER: return "Tracker";
                case DeviceType::TRACKING_REFERENCE: return "TrackingReference";
                default: return "Unknown";
            }
        }

        template <typename T>
        void AppendRaw(std::vector<uint8_t>& out, const T& value) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), p, p + sizeof(T));
        }
    }

    DatasetRecorder::~DatasetRecorder() {
        StopSession();
    }

    std::string DatasetRecorder::DefaultBaseDir() {
        return GetAppDataPath() + "/datasets";
    }

    bool DatasetRecorder::StartSession(const std::string& base_dir, const std::string& source) {
        if (recording_) {
            Logger::Warning("DatasetRecorder: StartSession called while already recording");
            return true;
        }

        std::error_code ec;
        fs::create_directories(base_dir, ec);

        std::string dir = base_dir + "/" + LocalTimestampForDirName();
        // Avoid collision if two sessions start within the same second.
        if (fs::exists(dir, ec)) {
            for (int suffix = 2; suffix < 100; ++suffix) {
                std::string candidate = dir + "_" + std::to_string(suffix);
                if (!fs::exists(candidate, ec)) { dir = candidate; break; }
            }
        }
        if (!fs::create_directories(dir, ec) && ec) {
            Logger::Error("DatasetRecorder: failed to create session dir " + dir + ": " + ec.message());
            return false;
        }

        pose_file_.open(dir + "/poses.bin", std::ios::binary | std::ios::trunc);
        if (!pose_file_.is_open()) {
            Logger::Error("DatasetRecorder: failed to open poses.bin in " + dir);
            return false;
        }

        // File header: magic + version + reserved.
        pose_file_.write(kMagic, sizeof(kMagic));
        uint32_t version = kFormatVersion, reserved = 0;
        pose_file_.write(reinterpret_cast<const char*>(&version), sizeof(version));
        pose_file_.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));

        session_dir_ = dir;
        source_ = source;
        session_start_wall_ = NowWall();
        session_start_mono_ = NowMono();
        device_indices_.clear();
        device_table_.clear();
        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            pauses_.clear();
        }
        frames_written_ = 0;
        samples_written_ = 0;
        bytes_written_ = sizeof(kMagic) + sizeof(version) + sizeof(reserved);
        frames_dropped_ = 0;
        paused_ = false;
        stop_requested_ = false;

        WriteManifest(false);

        recording_ = true;
        writer_thread_ = std::thread(&DatasetRecorder::WriterThreadMain, this);

        Logger::Info("DatasetRecorder: recording to " + dir);
        return true;
    }

    void DatasetRecorder::StopSession() {
        if (!recording_) {
            return;
        }

        // Close any open pause segment so the manifest is consistent.
        SetPaused(false);

        recording_ = false;
        stop_requested_ = true;
        queue_cv_.notify_all();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }

        WriteManifest(true);
        pose_file_.close();
        Logger::Info("DatasetRecorder: session finalized: " + session_dir_ +
                     " (" + std::to_string(frames_written_.load()) + " frames)");
    }

    void DatasetRecorder::SetPaused(bool paused) {
        if (!recording_ || paused_ == paused) {
            return;
        }
        paused_ = paused;

        std::lock_guard<std::mutex> lock(pause_mutex_);
        if (paused) {
            PauseSegment seg;
            seg.start_wall = NowWall();
            pauses_.push_back(seg);
        } else if (!pauses_.empty() && pauses_.back().end_wall == 0.0) {
            pauses_.back().end_wall = NowWall();
        }
    }

    double DatasetRecorder::PausedSecondsTotal() const {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        double total = 0.0;
        for (const auto& seg : pauses_) {
            double end = (seg.end_wall > 0.0) ? seg.end_wall : NowWall();
            total += end - seg.start_wall;
        }
        return total;
    }

    std::string DatasetRecorder::SessionDir() const {
        return session_dir_;
    }

    void DatasetRecorder::OnDeviceUpdate(const std::vector<DevicePositionData>& devices) {
        if (!recording_ || paused_ || devices.empty()) {
            return;
        }

        QueuedFrame frame;
        frame.devices = devices; // copy; the callback's vector is reused upstream
        frame.recv_mono = NowMono();
        frame.recv_wall = NowWall();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_.size() >= kMaxQueuedFrames) {
                // Disk can't keep up (or is gone). Drop and count rather than
                // grow without bound; the manifest reports the loss honestly.
                frames_dropped_++;
                return;
            }
            queue_.push_back(std::move(frame));
        }
        queue_cv_.notify_one();
    }

    uint16_t DatasetRecorder::DeviceIndexFor(const DevicePositionData& device) {
        auto it = device_indices_.find(device.serial);
        if (it != device_indices_.end()) {
            return it->second;
        }
        uint16_t index = static_cast<uint16_t>(device_table_.size());
        device_indices_[device.serial] = index;
        device_table_.push_back({device.serial, static_cast<uint8_t>(device.type)});
        return index;
    }

    void DatasetRecorder::WriteFrame(const QueuedFrame& frame) {
        // Frame record:
        //   [f64 sample_wall][f64 sample_mono][f64 recv_wall][f64 recv_mono]
        //   [u16 device_count]
        //   per device: [u16 index][u8 flags][u8 tracking_result]
        //               [3f pos][4f quat][3f vel][3f angvel]
        // sample_* come from the driver (0.0 on legacy v1 messages); recv_*
        // are stamped at pipe receipt and always present.
        std::vector<uint8_t> buf;
        buf.reserve(34 + frame.devices.size() * 56);

        const auto& first = frame.devices.front();
        AppendRaw(buf, first.sample_time_wall);
        AppendRaw(buf, first.sample_time_mono);
        AppendRaw(buf, frame.recv_wall);
        AppendRaw(buf, frame.recv_mono);
        AppendRaw(buf, static_cast<uint16_t>(frame.devices.size()));

        for (const auto& device : frame.devices) {
            AppendRaw(buf, DeviceIndexFor(device));
            uint8_t flags = (device.connected ? 0x01 : 0x00) |
                            (device.pose_valid ? 0x02 : 0x00);
            AppendRaw(buf, flags);
            AppendRaw(buf, device.tracking_result);
            for (int i = 0; i < 3; ++i) AppendRaw(buf, device.position[i]);
            for (int i = 0; i < 4; ++i) AppendRaw(buf, device.rotation[i]);
            for (int i = 0; i < 3; ++i) AppendRaw(buf, device.velocity[i]);
            for (int i = 0; i < 3; ++i) AppendRaw(buf, device.angular_velocity[i]);
        }

        pose_file_.write(reinterpret_cast<const char*>(buf.data()),
                         static_cast<std::streamsize>(buf.size()));

        frames_written_++;
        samples_written_ += frame.devices.size();
        bytes_written_ += buf.size();
    }

    void DatasetRecorder::WriterThreadMain() {
        double last_flush = NowMono();
        double last_manifest = NowMono();

        for (;;) {
            std::deque<QueuedFrame> batch;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait_for(lock, std::chrono::milliseconds(250), [this] {
                    return !queue_.empty() || stop_requested_;
                });
                batch.swap(queue_);
            }

            for (const auto& frame : batch) {
                WriteFrame(frame);
            }

            double now = NowMono();
            if (now - last_flush >= kFlushSeconds) {
                pose_file_.flush();
                last_flush = now;
            }
            if (now - last_manifest >= kManifestRewriteSeconds) {
                WriteManifest(false);
                last_manifest = now;
            }

            if (stop_requested_) {
                // One final drain in case frames arrived after the last swap.
                std::deque<QueuedFrame> rest;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    rest.swap(queue_);
                }
                for (const auto& frame : rest) {
                    WriteFrame(frame);
                }
                pose_file_.flush();
                break;
            }
        }
    }

    void DatasetRecorder::WriteManifest(bool final_write) {
        json j;
        j["format"] = "spvr-dataset";
        j["version"] = kFormatVersion;
        j["app_version"] = STAYPUTVR_VERSION;
        j["git_hash"] = STAYPUTVR_GIT_HASH;
        j["source"] = source_;
        j["started_wall"] = session_start_wall_;
        j["started_iso"] = WallToIso(session_start_wall_);
        j["updated_wall"] = NowWall();
        if (final_write) {
            j["ended_wall"] = NowWall();
        }
        j["clean_shutdown"] = final_write;
        j["frames"] = frames_written_.load();
        j["samples"] = samples_written_.load();
        j["bytes"] = bytes_written_.load();
        j["frames_dropped"] = frames_dropped_.load();

        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            double paused_total = 0.0;
            json pauses = json::array();
            for (const auto& seg : pauses_) {
                double end = (seg.end_wall > 0.0) ? seg.end_wall : NowWall();
                paused_total += end - seg.start_wall;
                pauses.push_back({{"start_wall", seg.start_wall},
                                  {"end_wall", seg.end_wall > 0.0 ? json(seg.end_wall) : json(nullptr)}});
            }
            j["paused_seconds"] = paused_total;
            j["pauses"] = pauses;
        }

        json devices = json::array();
        for (size_t i = 0; i < device_table_.size(); ++i) {
            devices.push_back({{"index", i},
                               {"serial", device_table_[i].serial},
                               {"type", DeviceTypeName(device_table_[i].type)}});
        }
        j["devices"] = devices;

        // Write to a temp file then rename so a crash mid-write never leaves a
        // truncated manifest.
        std::string path = session_dir_ + "/manifest.json";
        std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out.is_open()) {
                Logger::Error("DatasetRecorder: failed to write manifest " + tmp);
                return;
            }
            out << j.dump(2);
        }
        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (ec) {
            Logger::Error("DatasetRecorder: manifest rename failed: " + ec.message());
        }
    }

    std::vector<DatasetSessionInfo> DatasetRecorder::ScanSessions(const std::string& base_dir) {
        std::vector<DatasetSessionInfo> sessions;
        std::error_code ec;
        if (!fs::exists(base_dir, ec)) {
            return sessions;
        }

        for (const auto& entry : fs::directory_iterator(base_dir, ec)) {
            if (!entry.is_directory()) continue;
            std::string manifest_path = entry.path().string() + "/manifest.json";
            std::ifstream in(manifest_path);
            if (!in.is_open()) continue;

            try {
                json j = json::parse(in);
                DatasetSessionInfo info;
                info.dir_name = entry.path().filename().string();
                info.path = entry.path().string();
                info.started_iso = j.value("started_iso", "");
                double started = j.value("started_wall", 0.0);
                double ended = 0.0;
                if (j.contains("ended_wall") && j["ended_wall"].is_number()) {
                    ended = j["ended_wall"].get<double>();
                } else {
                    // Crashed / still-recording session: best effort from the
                    // last periodic manifest rewrite (loses at most ~15 s).
                    ended = j.value("updated_wall", started);
                }
                info.duration_total = (ended > started) ? ended - started : 0.0;
                info.duration_paused = j.value("paused_seconds", 0.0);
                info.frames = j.value("frames", static_cast<uint64_t>(0));
                info.samples = j.value("samples", static_cast<uint64_t>(0));
                info.bytes = j.value("bytes", static_cast<uint64_t>(0));
                info.clean_shutdown = j.value("clean_shutdown", false);
                info.source = j.value("source", "");
                if (j.contains("devices") && j["devices"].is_array()) {
                    info.device_count = static_cast<int>(j["devices"].size());
                }
                sessions.push_back(std::move(info));
            }
            catch (const std::exception& e) {
                Logger::Warning("DatasetRecorder: unreadable manifest in " +
                                entry.path().string() + ": " + e.what());
            }
        }

        // Newest first.
        std::sort(sessions.begin(), sessions.end(),
                  [](const DatasetSessionInfo& a, const DatasetSessionInfo& b) {
                      return a.dir_name > b.dir_name;
                  });
        return sessions;
    }
}
