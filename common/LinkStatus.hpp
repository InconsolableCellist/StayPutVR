#pragma once

#include <chrono>
#include <string>
#include <algorithm>

// Shared connection-status contract used by the communication integrations
// (OSC, OSCQuery, PiShock, OpenShock, Twitch) so the Status tab can render a
// consistent picture and so the persistent-connection managers can share a
// single reconnect-with-backoff policy.
//
// None of the types here are thread-safe; managers that mutate a LinkStatus or
// ReconnectBackoff from a worker thread must guard access with their own mutex
// (or copy under lock before returning a snapshot to the UI thread).

namespace StayPutVR {

    // High-level health of a single communication channel.
    enum class LinkState {
        Disabled,    // integration turned off in config
        Connecting,  // attempting to (re)connect, no success yet
        Connected,   // healthy
        Degraded,    // usable but stale/partial (e.g. no recent inbound traffic)
        Failed       // down (reconnecting, or gave up pending manual retry)
    };

    inline const char* ToString(LinkState state) {
        switch (state) {
            case LinkState::Disabled:   return "Disabled";
            case LinkState::Connecting: return "Connecting";
            case LinkState::Connected:  return "Connected";
            case LinkState::Degraded:   return "Degraded";
            case LinkState::Failed:     return "Failed";
        }
        return "Unknown";
    }

    // A snapshot of one channel's status, suitable for display on the Status tab.
    struct LinkStatus {
        LinkState state = LinkState::Disabled;
        std::string detail;        // human-readable, e.g. "VRChat not discovered"
        std::string last_error;    // last error message, if any
        std::chrono::steady_clock::time_point last_ok{};      // last confirmed-good moment
        std::chrono::steady_clock::time_point last_attempt{}; // last (re)connect attempt
        int consecutive_failures = 0;
        bool gave_up = false;      // exhausted retries; needs a manual "Reconnect now"
    };

    // Exponential backoff with a cap and a give-up threshold.
    //
    // Policy (chosen by the user): retry with growing delay up to a cap, then
    // after max_failures consecutive failures stop retrying entirely and wait
    // for an explicit Resume() (the Status tab's "Reconnect now" button).
    //
    // Typical use inside a manager's Update():
    //     if (!IsConnected()) {
    //         if (backoff_.ShouldAttempt(now)) {
    //             backoff_.OnAttempt(now);
    //             if (Connect()) backoff_.OnSuccess();
    //             else           backoff_.OnFailure();
    //         }
    //     } else {
    //         backoff_.OnSuccess();
    //     }
    class ReconnectBackoff {
    public:
        using clock = std::chrono::steady_clock;

        explicit ReconnectBackoff(
            std::chrono::milliseconds base = std::chrono::seconds(1),
            std::chrono::milliseconds cap  = std::chrono::seconds(30),
            int max_failures = 20)
            : base_(base), cap_(cap), max_failures_(max_failures) {}

        // True if a (re)connect attempt should be made now. False while still
        // within the backoff window or after we have given up.
        bool ShouldAttempt(clock::time_point now) const {
            if (gave_up_) return false;
            return now >= next_attempt_;
        }

        // Mark that an attempt is starting now; schedules the next eligible slot
        // based on the current failure count.
        void OnAttempt(clock::time_point now) {
            next_attempt_ = now + CurrentDelay();
        }

        // Connection succeeded: clear all backoff state.
        void OnSuccess() {
            failures_ = 0;
            gave_up_ = false;
            next_attempt_ = clock::time_point{};
        }

        // Connection attempt failed: grow the delay and, once the threshold is
        // reached, give up until the user asks to resume.
        void OnFailure() {
            if (failures_ < max_failures_) {
                ++failures_;
            }
            if (failures_ >= max_failures_) {
                gave_up_ = true;
            }
        }

        // User requested a manual reconnect: clear the give-up flag and allow an
        // attempt immediately.
        void Resume() {
            failures_ = 0;
            gave_up_ = false;
            next_attempt_ = clock::time_point{};
        }

        bool GaveUp() const { return gave_up_; }
        int  Failures() const { return failures_; }

        // base * 2^failures, clamped to cap.
        std::chrono::milliseconds CurrentDelay() const {
            int shift = std::min(failures_, 20); // avoid shift overflow
            long long ms = base_.count() * (1LL << shift);
            return std::chrono::milliseconds(std::min<long long>(ms, cap_.count()));
        }

    private:
        std::chrono::milliseconds base_;
        std::chrono::milliseconds cap_;
        int max_failures_;
        int failures_ = 0;
        bool gave_up_ = false;
        clock::time_point next_attempt_{};
    };

} // namespace StayPutVR
