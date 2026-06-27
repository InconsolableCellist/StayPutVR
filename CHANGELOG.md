# Changelog

All notable user-facing changes to StayPutVR are documented here. Dates are M/D/YYYY.

## 1.5 — Jaw + Microphone restraint, unified collar mode (6/26/2026)

**Requires the new 1.5 avatar prefab** — this release adds/renames synced OSC
parameters (`SPVR_Mic_Status`, `SPVR_Collar_Mode`, `SPVR_SoundEffect`) and retires
the `SPVR_JawEnabled` radial in favor of the unified collar toggle.

### Added
- **VRCFT JawOpen constraint** — with VRCFaceTracking, lock your collar and your
  jaw must stay where it was when locked (mouth held open or closed); straying too
  far escalates warning → disobedience like the position constraint. Driven by the
  `SPVR_JawOpen` bridge param; configured on the new Integrations → VRCFT tab and the
  Devices → Visual head slot.
- **Microphone enforced-mute constraint** — while locked, your microphone must stay
  near the ambient room level captured at lock time (i.e. stay quiet); talking too
  loud trips warning → disobedience. Adaptive: captures the room's noise floor during
  a grace window. Includes a **Calibrate** button that samples a few seconds of
  background noise and sets the thresholds above it (for noisy rooms), a configurable
  post-disobedience cooldown, and a live VU meter with a decaying peak-hold.
  Windows-only capture (WASAPI) with automatic device reconnect. New Integrations →
  Mic tab.
- **Unified collar mode** — one in-game momentary button (`SPVR_Collar_ToggleButton`)
  cycles `SPVR_Collar_Mode` between Neither / Jaw / Mic / Both, skipping any feature
  you haven't enabled. Replaces the per-feature `SPVR_JawEnabled` radial.
- **In-game sound effects** — on lock, unlock, warning, disobedience, and collar-mode
  switch, the app can pulse an int enum on `SPVR_SoundEffect` so an avatar animation
  layer plays a sound. Per-event toggles (on by default) under
  Settings → Notifications → "In-Game Sound Effects".
- **PiShock warning-zone actions** — the PiShock Actions tab now has a full Warning
  Zone section (beep / vibrate / shock with their own intensity & duration), parallel
  to the Out of Bounds section.
- Status tab now shows live Jaw and Mic rows alongside tracked devices, plus a
  collar-mode and mic-level readout.

### Changed
- Settings → Notifications: "Audio Notifications" renamed to **"App Sound Effects"**
  (the on-PC cues), distinct from the new in-game sound effects.
- PiShock intensity controls (warning + disobedience, master & per-device) now have
  +/- nudge buttons that step in small increments.

### Fixed
- **Warnings no longer starve the disobedience shock** — warning and disobedience
  actions previously shared one rate-limit timer, so repeated warnings could prevent
  the disobedience shock from ever firing. They now throttle independently.
- PiShock warnings honored their config instead of firing a hardcoded beep + the
  *disobedience* vibrate; warnings are now silent unless explicitly configured.
- JawOpen input callbacks are registered on startup auto-connect (not only on a manual
  OSC toggle), so the jaw value/constraint works on a normal launch.
- OSC inbound-callback registration consolidated into one place so the startup and
  reconnect paths can't drift (this also fixed per-device shock intensities not
  applying until an OSC toggle).
- Collar toggle now has a time debounce so contact bounce / rapid taps don't
  multi-advance the mode.

## 1.4 — Bug fixes, PiShock v2 default, fewer synced params, UI overhaul (6/22/2026)
- PiShock WebSocket v2 is now the default for new users (existing users keep their saved setting)
- Fixed PiShock multi-action: Beep + Vibrate + Shock now all fire from a single event (#9)
- Changing avatars now unlocks and resets all device status instead of leaving it stale (#6)
- Added an OSC `Shock` parameter and surfaced the Bite shock in the UI — both have their own intensity/duration and are blocked while emergency stop is active (#7)
- Added OSCQuery (mDNS) auto-discovery so StayPutVR no longer fights other apps over the OSC receive port (#8); can be turned off to use manual ports
- Reduced synced avatar parameters: device status is now sent as 3 bools per device (15 synced bits) instead of 5 synced ints (40 bits) — **requires the new 1.4 avatar prefab**
- Redesigned the Devices tab: drag-and-drop role assignment onto an avatar effigy, a per-device movement "heat" meter for identifying trackers, and a scaled radial zone map (a classic list view is still available)
- Reorganized the interface into Status / Devices / Integrations / Settings tabs
- OSC is now enabled by default, with advanced path settings collapsed behind clearer defaults and per-section reset buttons
- Settings now auto-save instantly and include an adjustable UI font scale
- Fixed config location: settings always load/save from `%APPDATA%\StayPutVR\config` (existing configs migrated automatically)
- Refreshed theme, updated Dear ImGui, added a Linux development build

## 1.3.2 — PiShock duration fix (5/21/2026)
- Fixed bug where PiShock warning/shock duration of 1.0s was resetting to 15s on restart

## 1.3.1 — Tech debt sprint (4/13/2026)
- Internal: guarded Twitch OAuth tokens with a mutex; fixed runtime hazards, extracted UI panels, added base classes

## 1.3.0 — BPIO integration (11/3/2025)
- Added BPIO integration (experimental)

## 1.2.0 — PiShock WebSocket v2 support (10/27/2025)
- Added PiShock WebSocket v2 support (faster response times, multi-device support)
- Fixed a bug where warning OSC messages ("Locked Warning", enum 3) weren't being sent

## 1.1.1 — Bug fixes (10/12/2025)
- Fixed driver not loading when launching SteamVR via SteamLink or Pico Connect
- Fixed PiShock warning/disobedience durations shown as 0.0-1.0 instead of 1.0-15.0 seconds
- Added shock cooldown timer for both PiShock and OpenShock

## 1.1.0 — Multi-shocker support (9/21/2025)
- Added support for multiple shockers using OpenShock
- Added emergency stop mode (OSC message)

## 1.0.3 — OpenShock Support (Beta) (8/25/2025)
- OpenShock integration equivalent to PiShock
- Fixed default OSC receive port (9005 → 9001, VRChat default)

## 1.0.2 — BiteTech support (8/24/2025)
- Added support for BiteTech devices (bite detection and disobedience actions)

## 1.0.1 — Performance & Stability (8/23/2025)
- Eliminated potential blocking operations in the VR driver main loop
- Resolved `VRApplicationError_IPCFailed` crashes from IPC timeouts after headset reconnects

## 1.0.0 — Initial release (8/8/2025)
