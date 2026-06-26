#include "MicrophoneManager.hpp"
#include "../../../common/Logger.hpp"

#include <chrono>
#include <cmath>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>
// Define the property-key GUIDs locally so we don't have to link propsys.lib just
// for PKEY_Device_FriendlyName. This is the only TU that includes the devpkey
// header, so there's no duplicate-definition risk. (__uuidof handles the COM
// interface IIDs, so no INITGUID is needed for those.)
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

namespace {

    std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 0) return std::wstring();
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    }

    std::string WideToUtf8(const wchar_t* w) {
        if (!w) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string s(static_cast<size_t>(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
        return s;
    }

    std::string FriendlyName(IMMDevice* device) {
        std::string name = "Unknown";
        if (!device) return name;
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                pv.vt == VT_LPWSTR && pv.pwszVal) {
                name = WideToUtf8(pv.pwszVal);
            }
            PropVariantClear(&pv);
            props->Release();
        }
        return name;
    }

    // Decide whether a negotiated mix format carries IEEE float or PCM int samples,
    // and the bit depth, so the capture loop can convert frames to a mono float.
    void DescribeFormat(const WAVEFORMATEX* wf, bool& is_float, int& bits) {
        is_float = true;
        bits = 32;
        if (!wf) return;
        bits = wf->wBitsPerSample;
        if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            is_float = true;
        } else if (wf->wFormatTag == WAVE_FORMAT_PCM) {
            is_float = false;
        } else if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                   wf->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
            is_float = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        }
    }

    // Linear amplitude (0..~1) -> 0..1 over a -60dB..0dB window for the VU/level.
    float AmplitudeToUnit(float amp) {
        if (amp <= 1e-6f) return 0.0f;
        float db = 20.0f * std::log10(amp);
        float u = (db + 60.0f) / 60.0f;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        return u;
    }

} // namespace

namespace StayPutVR {

    MicrophoneManager::~MicrophoneManager() {
        Stop();
    }

    bool MicrophoneManager::Start() {
        if (running_.exchange(true)) return true;  // already running
        capture_thread_ = std::thread(&MicrophoneManager::CaptureLoop, this);
        return true;
    }

    void MicrophoneManager::Stop() {
        if (!running_.exchange(false)) return;
        if (capture_thread_.joinable()) capture_thread_.join();
        connected_.store(false);
        level_.store(0.0f);
    }

    void MicrophoneManager::SetDevice(const std::string& device_id) {
        bool was_running = running_.load();
        if (was_running) Stop();
        {
            std::lock_guard<std::mutex> lk(meta_mutex_);
            selected_device_id_ = device_id;
        }
        if (was_running) Start();
    }

    std::string MicrophoneManager::GetCurrentDeviceId() const {
        std::lock_guard<std::mutex> lk(meta_mutex_);
        return selected_device_id_;
    }

    std::string MicrophoneManager::GetCurrentDeviceName() const {
        std::lock_guard<std::mutex> lk(meta_mutex_);
        return current_device_name_;
    }

    std::string MicrophoneManager::GetLastError() const {
        std::lock_guard<std::mutex> lk(meta_mutex_);
        return last_error_;
    }

    void MicrophoneManager::SetError(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(meta_mutex_);
            last_error_ = msg;
        }
        Logger::Warning("Mic: " + msg);
    }

    std::vector<MicAudioDevice> MicrophoneManager::GetDevices() {
        std::vector<MicAudioDevice> out;
        HRESULT hrco = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool co = SUCCEEDED(hrco) || hrco == RPC_E_CHANGED_MODE;

        IMMDeviceEnumerator* en = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))) && en) {
            IMMDeviceCollection* coll = nullptr;
            if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &coll)) && coll) {
                UINT count = 0;
                coll->GetCount(&count);
                for (UINT i = 0; i < count; ++i) {
                    IMMDevice* d = nullptr;
                    if (SUCCEEDED(coll->Item(i, &d)) && d) {
                        LPWSTR id = nullptr;
                        std::string sid;
                        if (SUCCEEDED(d->GetId(&id)) && id) { sid = WideToUtf8(id); CoTaskMemFree(id); }
                        out.push_back({ sid, FriendlyName(d) });
                        d->Release();
                    }
                }
                coll->Release();
            }
            en->Release();
        }
        if (co && hrco != RPC_E_CHANGED_MODE) CoUninitialize();
        return out;
    }

    bool MicrophoneManager::InitDevice() {
        std::string want_id;
        {
            std::lock_guard<std::mutex> lk(meta_mutex_);
            want_id = selected_device_id_;
        }

        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || !enumerator) { SetError("CoCreateInstance(MMDeviceEnumerator) failed"); return false; }

        IMMDevice* device = nullptr;
        if (!want_id.empty()) {
            std::wstring wid = Utf8ToWide(want_id);
            hr = enumerator->GetDevice(wid.c_str(), &device);
            if (FAILED(hr) || !device) {
                Logger::Warning("Mic: saved capture device not found, falling back to default");
                { std::lock_guard<std::mutex> lk(meta_mutex_); selected_device_id_.clear(); }
                hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
            }
        } else {
            hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        }
        if (FAILED(hr) || !device) { SetError("no capture device available"); enumerator->Release(); return false; }

        {
            std::string nm = FriendlyName(device);
            std::lock_guard<std::mutex> lk(meta_mutex_);
            current_device_name_ = nm;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&audio_client_));
        if (FAILED(hr) || !audio_client_) { SetError("Activate(IAudioClient) failed"); device->Release(); enumerator->Release(); return false; }

        WAVEFORMATEX* mix = nullptr;
        audio_client_->GetMixFormat(&mix);

        HRESULT init = E_FAIL;
        if (mix) {
            init = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, mix, nullptr);
            if (SUCCEEDED(init)) {
                source_sample_rate_ = static_cast<int>(mix->nSamplesPerSec);
                source_channels_ = mix->nChannels;
                DescribeFormat(mix, source_is_float_, source_bits_);
            }
        }

        if (FAILED(init)) {
            // Format-negotiation cascade: try common float32 rates.
            static const DWORD kRates[] = { 48000, 44100, 96000 };
            for (DWORD rate : kRates) {
                WAVEFORMATEX fb{};
                fb.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                fb.nChannels = 1;
                fb.nSamplesPerSec = rate;
                fb.wBitsPerSample = 32;
                fb.nBlockAlign = static_cast<WORD>((fb.nChannels * fb.wBitsPerSample) / 8);
                fb.nAvgBytesPerSec = fb.nSamplesPerSec * fb.nBlockAlign;
                fb.cbSize = 0;
                WAVEFORMATEX* closest = nullptr;
                HRESULT sup = audio_client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &fb, &closest);
                WAVEFORMATEX* use = (sup == S_OK) ? &fb : closest;
                if (use) {
                    init = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, use, nullptr);
                    if (SUCCEEDED(init)) {
                        source_sample_rate_ = static_cast<int>(use->nSamplesPerSec);
                        source_channels_ = use->nChannels;
                        DescribeFormat(use, source_is_float_, source_bits_);
                        Logger::Info("Mic: using fallback capture format " +
                                     std::to_string(source_sample_rate_) + "Hz " +
                                     std::to_string(source_channels_) + "ch");
                    }
                }
                if (closest) CoTaskMemFree(closest);
                if (SUCCEEDED(init)) break;
            }
        }

        if (mix) CoTaskMemFree(mix);

        if (FAILED(init)) {
            SetError("IAudioClient::Initialize failed for all formats");
            audio_client_->Release(); audio_client_ = nullptr;
            device->Release(); enumerator->Release();
            return false;
        }

        hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                       reinterpret_cast<void**>(&capture_client_));
        if (FAILED(hr) || !capture_client_) {
            SetError("GetService(IAudioCaptureClient) failed");
            audio_client_->Release(); audio_client_ = nullptr;
            device->Release(); enumerator->Release();
            return false;
        }

        hr = audio_client_->Start();
        if (FAILED(hr)) {
            SetError("IAudioClient::Start failed");
            ReleaseDevice();
            device->Release(); enumerator->Release();
            return false;
        }

        device->Release();
        enumerator->Release();
        {
            std::lock_guard<std::mutex> lk(meta_mutex_);
            Logger::Info("Mic: capture device opened: " + current_device_name_ + " (" +
                         std::to_string(source_sample_rate_) + "Hz, " +
                         std::to_string(source_channels_) + "ch, " +
                         (source_is_float_ ? "float" : ("int" + std::to_string(source_bits_))) + ")");
        }
        return true;
    }

    void MicrophoneManager::ReleaseDevice() {
        if (capture_client_) { capture_client_->Release(); capture_client_ = nullptr; }
        if (audio_client_) { audio_client_->Stop(); audio_client_->Release(); audio_client_ = nullptr; }
    }

    float MicrophoneManager::FrameToMono(const unsigned char* data, unsigned int frame) const {
        const int ch = source_channels_ > 0 ? source_channels_ : 1;
        double acc = 0.0;
        for (int c = 0; c < ch; ++c) {
            float s = 0.0f;
            const size_t idx = static_cast<size_t>(frame) * ch + c;
            if (source_is_float_) {
                s = reinterpret_cast<const float*>(data)[idx];
            } else if (source_bits_ == 16) {
                s = reinterpret_cast<const int16_t*>(data)[idx] / 32768.0f;
            } else if (source_bits_ == 32) {
                s = reinterpret_cast<const int32_t*>(data)[idx] / 2147483648.0f;
            }
            // 24-bit packed PCM is not handled (shared-mode mix is virtually always
            // 32-bit float); such a device contributes 0 and the VU simply reads low.
            acc += s;
        }
        return static_cast<float>(acc / ch);
    }

    void MicrophoneManager::CaptureLoop() {
        HRESULT hrco = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool co = SUCCEEDED(hrco) || hrco == RPC_E_CHANGED_MODE;
        bool logged_fail = false;

        while (running_.load()) {
            if (!capture_client_) {
                if (!InitDevice()) {
                    connected_.store(false);
                    if (!logged_fail) { Logger::Warning("Mic: capture device unavailable, will retry"); logged_fail = true; }
                    // Back off ~1s but stay responsive to Stop().
                    for (int i = 0; i < 10 && running_.load(); ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                connected_.store(true);
                logged_fail = false;
            }

            UINT32 packet = 0;
            HRESULT hr = capture_client_->GetNextPacketSize(&packet);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
                    Logger::Warning("Mic: capture device invalidated, reconnecting");
                } else {
                    SetError("GetNextPacketSize failed");
                }
                ReleaseDevice();
                connected_.store(false);
                continue;  // re-init next iteration
            }

            if (packet == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            bool device_lost = false;
            while (packet > 0 && running_.load()) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = capture_client_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) device_lost = true;
                    break;
                }

                double sumsq = 0.0;
                if (frames > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    for (UINT32 i = 0; i < frames; ++i) {
                        float mono = FrameToMono(data, i);
                        sumsq += static_cast<double>(mono) * mono;
                    }
                }
                float rms = frames > 0 ? static_cast<float>(std::sqrt(sumsq / frames)) : 0.0f;
                float target = AmplitudeToUnit(rms);

                // One-pole smoothing so the per-frame UI read and the constraint's
                // baseline comparison are stable; flush denormals to zero.
                float prev = level_.load(std::memory_order_relaxed);
                float smoothed = 0.8f * prev + 0.2f * target;
                if (smoothed < 1e-7f) smoothed = 0.0f;
                level_.store(smoothed, std::memory_order_relaxed);

                capture_client_->ReleaseBuffer(frames);

                if (FAILED(capture_client_->GetNextPacketSize(&packet))) { device_lost = true; break; }
            }

            if (device_lost) {
                Logger::Warning("Mic: capture buffer error, reconnecting");
                ReleaseDevice();
                connected_.store(false);
            }
        }

        ReleaseDevice();
        if (co && hrco != RPC_E_CHANGED_MODE) CoUninitialize();
    }

} // namespace StayPutVR

#else  // !_WIN32 — no-op stub for the Linux dev build.

namespace StayPutVR {
    MicrophoneManager::~MicrophoneManager() { Stop(); }
    bool MicrophoneManager::Start() { return false; }
    void MicrophoneManager::Stop() { running_.store(false); }
    void MicrophoneManager::SetDevice(const std::string& device_id) {
        std::lock_guard<std::mutex> lk(meta_mutex_); selected_device_id_ = device_id;
    }
    std::string MicrophoneManager::GetCurrentDeviceId() const {
        std::lock_guard<std::mutex> lk(meta_mutex_); return selected_device_id_;
    }
    std::string MicrophoneManager::GetCurrentDeviceName() const {
        std::lock_guard<std::mutex> lk(meta_mutex_); return current_device_name_;
    }
    std::string MicrophoneManager::GetLastError() const {
        std::lock_guard<std::mutex> lk(meta_mutex_); return last_error_;
    }
    std::vector<MicAudioDevice> MicrophoneManager::GetDevices() { return {}; }
} // namespace StayPutVR

#endif
