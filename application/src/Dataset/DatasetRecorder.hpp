#pragma once
//
// DatasetRecorder — lossless raw tracker-pose capture for training datasets.
//
// Philosophy: record EVERYTHING, filter at training time. Tracking dropouts,
// drift, AFK periods, and left-on-overnight sessions are data, not noise —
// they are exactly what a downstream model needs to learn "what AFK looks
// like". The recorder therefore never filters, deduplicates, or downsamples;
// the only user control is pause (a privacy affordance, logged as segments).
//
// Threading: OnDeviceUpdate() is called from the IPC reader thread (~90 Hz,
// per SteamVR driver frame) and must stay cheap — it copies the batch into a
// mutex-guarded queue. A dedicated writer thread drains the queue to disk and
// periodically rewrites the manifest so a crash loses at most a few seconds
// of bookkeeping (the pose file itself is append-only and survives).
//
// On-disk layout, one directory per session:
//   <base_dir>/<YYYY-MM-DD_HH-MM-SS>/
//     poses.bin       binary pose stream (see DATASET_FORMAT.md / kMagic)
//     manifest.json   session metadata: devices, counts, pauses, times
//
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../../common/DeviceTypes.hpp"

namespace StayPutVR {

    // Summary of an on-disk session, parsed from its manifest for the UI table.
    struct DatasetSessionInfo {
        std::string dir_name;        // e.g. "2026-07-15_18-04-31"
        std::string path;            // absolute session directory
        std::string started_iso;     // human-readable start time
        double duration_total = 0.0; // wall seconds, start..end (or last update)
        double duration_paused = 0.0;
        uint64_t frames = 0;
        uint64_t samples = 0;
        uint64_t bytes = 0;
        int device_count = 0;
        bool clean_shutdown = false; // false => app crashed / was killed mid-session
        std::string source;          // "steamvr-driver" or "simulator"
    };

    class DatasetRecorder {
    public:
        DatasetRecorder() = default;
        ~DatasetRecorder();

        DatasetRecorder(const DatasetRecorder&) = delete;
        DatasetRecorder& operator=(const DatasetRecorder&) = delete;

        // Begin a new session under base_dir (created if missing). Returns false
        // if the directory or pose file could not be created.
        bool StartSession(const std::string& base_dir, const std::string& source);
        // Finalize: drain the queue, write the closing manifest, join the writer.
        void StopSession();

        // Pause/resume capture. Paused intervals are recorded in the manifest.
        void SetPaused(bool paused);

        bool IsRecording() const { return recording_; }
        bool IsPaused() const { return paused_; }

        // Ingest one driver frame. Safe to call from any thread at any rate;
        // drops nothing while recording and unpaused, no-ops otherwise.
        void OnDeviceUpdate(const std::vector<DevicePositionData>& devices);

        // Live stats for the UI (all monotonic within a session).
        uint64_t FramesWritten() const { return frames_written_; }
        uint64_t SamplesWritten() const { return samples_written_; }
        uint64_t BytesWritten() const { return bytes_written_; }
        uint64_t FramesDropped() const { return frames_dropped_; }
        double SessionStartWall() const { return session_start_wall_; }
        double PausedSecondsTotal() const;
        std::string SessionDir() const;
        // Source tag of the active session ("steamvr-driver" or "simulator").
        std::string Source() const { return source_; }

        // Scan base_dir for session directories with manifests. Static so the
        // UI can list sessions without a live recorder.
        static std::vector<DatasetSessionInfo> ScanSessions(const std::string& base_dir);

        // Default dataset root: <AppData>/StayPutVR/datasets
        static std::string DefaultBaseDir();

        // poses.bin header magic ("SPVRDS01").
        static constexpr char kMagic[8] = {'S', 'P', 'V', 'R', 'D', 'S', '0', '1'};
        static constexpr uint32_t kFormatVersion = 1;

    private:
        struct QueuedFrame {
            std::vector<DevicePositionData> devices;
            double recv_mono = 0.0; // steady_clock at enqueue (receipt time)
            double recv_wall = 0.0; // system_clock at enqueue
        };

        struct PauseSegment {
            double start_wall = 0.0;
            double end_wall = 0.0; // 0 while the pause is still open
        };

        void WriterThreadMain();
        void WriteFrame(const QueuedFrame& frame);
        // Serialize current bookkeeping to manifest.json (called from the
        // writer thread and from StopSession).
        void WriteManifest(bool final_write);
        uint16_t DeviceIndexFor(const DevicePositionData& device);

        // --- Session state (owned by writer thread once started) ---
        std::ofstream pose_file_;
        std::string session_dir_;
        std::string source_;
        double session_start_wall_ = 0.0;
        double session_start_mono_ = 0.0;

        // Device registry: serial -> stable index within this session.
        std::unordered_map<std::string, uint16_t> device_indices_;
        struct DeviceEntry { std::string serial; uint8_t type; };
        std::vector<DeviceEntry> device_table_;

        std::vector<PauseSegment> pauses_;
        mutable std::mutex pause_mutex_;

        // --- Queue between ingest (IPC reader thread) and writer thread ---
        std::deque<QueuedFrame> queue_;
        std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        // Backstop only: if the disk stalls hard the queue caps out and we drop
        // (and count) frames rather than eat unbounded memory. At 90 Hz this is
        // ~90 seconds of buffer.
        static constexpr size_t kMaxQueuedFrames = 8192;

        std::thread writer_thread_;
        std::atomic<bool> recording_{false};
        std::atomic<bool> paused_{false};
        std::atomic<bool> stop_requested_{false};

        // --- Stats (atomics: written by writer thread, read by UI thread) ---
        std::atomic<uint64_t> frames_written_{0};
        std::atomic<uint64_t> samples_written_{0};
        std::atomic<uint64_t> bytes_written_{0};
        std::atomic<uint64_t> frames_dropped_{0};

        static constexpr double kManifestRewriteSeconds = 15.0;
        static constexpr double kFlushSeconds = 5.0;
    };
}
