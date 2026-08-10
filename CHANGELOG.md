# Changelog

All notable user-facing changes to StayPutVR are documented here. Dates are M/D/YYYY.

## 1.5.1 — Bite zones (8/9/2026)

### New features
- **Bite zones:** the prefab now reports which body part was bitten by sending a
  suffixed parameter alongside the plain `SPVR_Bite` — `SPVR_Bite_Tail`,
  `SPVR_Bite_Ear_Left`, `SPVR_Bite_Ear_Right`, `SPVR_Bite_Thigh_Left`,
  `SPVR_Bite_Thigh_Right`, `SPVR_Bite_Jaw`. Each zone can be bound to its own
  shockers, DG-Lab channels and BPIO toys, so somebody running several
  integrations can have a tail bite hit one device and an ear bite another. Bind
  them in Devices → Visual by switching the view to **Bite zones** and dragging
  the same ID chips you use for tracker cuffs onto a body part; every zone also
  carries its own intensity and duration.
- The routing is opt-in via **Route bites by body part** on Integrations → OSC
  Triggers. With it off — and for any zone nothing is bound to — a bite fires
  every configured device at the global Bite intensity/duration, exactly as in
  1.5.0. Existing configs are unaffected until the box is ticked.
- **BPIO toys now take part in bites** when bound to a zone, as a one-shot pulse
  for the zone's duration. The unrouted path is unchanged (shockers only), so
  toys never start buzzing on bites nobody asked them to.
- The body-part parameters are the configured bite path plus a fixed suffix, so
  renaming the bite path renames the whole family. They are listed read-only
  under Settings → OSC → Bite Trigger and advertised over OSCQuery.
- Bites are coalesced over a short window before firing. A prefab that reports
  the body part may also send the plain `SPVR_Bite` for the same bite, and
  VRChat delivers the two as separate messages in no guaranteed order — acting
  on each as it landed would shock twice, and could act on the unspecific one
  first. One bite now fires once, using the most specific parameter received.
- Each bite zone has a **Test** button in its config panel that fires exactly
  what an inbound bite there would, without counting toward the bite tally.

## 1.5.0 — Lock-enforcement and safety fixes, shocker names, bite counter (8/4/2026)

### New features
- **Name your shockers:** each PiShock and OpenShock slot now takes an optional
  friendly name ("Left ankle", "Collar") next to its ID on the Integrations →
  PiShock / OpenShock tabs. The name replaces the bare 0–4 slot number wherever you
  bind that shocker, and shows on hover for the compact chips in the Devices tab.
  Leave it blank and everything reads exactly as it did before. (#10)
- **Bite counter:** the Integrations → OSC Triggers tab now tracks how many bites
  you've taken this session and over all time, with a Reset button. Only bites that
  actually fire are counted — ones ignored because the trigger is off or emergency
  stop is active don't inflate the total. (#16)

### Bug fixes
- **Chaining mode no longer re-locks continuously:** VRChat re-sends avatar
  parameters (on avatar load, world join, and periodically from many OSC senders),
  and every repeat of a still-held lock latch was treated as a brand-new lock
  request. That re-captured each device's anchor position — so a "locked" tracker's
  reference point silently drifted to wherever it currently was — replayed the lock
  cue, and with chaining mode on re-fired the global lock, undoing an unlock you had
  just done in the UI and re-engaging the jaw/mic collar gate with it. Lock
  parameters are now acted on only when they actually change. (#11)
- **Safe mode no longer shows phantom locks:** during emergency stop, and for a
  device auto-released past the disable distance, the deferred status updates (bite
  timer, global out-of-bounds timer, avatar re-sync) could re-report the device as
  locked — the cuff turned red on your avatar while nothing was actually being
  enforced. A lock request that gets refused during emergency stop now also pushes
  the true unlocked state back, instead of being dropped silently. (#13)
- **Emergency stop covers the global out-of-bounds trigger:** receiving the global
  out-of-bounds parameter while emergency stop was latched still fired your
  PiShock / OpenShock / DG-Lab disobedience actions. It is now blocked like every
  other trigger.
- **OSCQuery CPU usage:** the mDNS discovery and advertisement loops spun two CPU
  cores continuously whenever OSC Query was enabled, because the socket timeouts
  they relied on were being silently ignored. They now block properly and sit near
  idle. (#15)

## 1.4.2 — DG-Lab Coyote, Enforced Unmute + bug fixes (7/23/2026)

### New features
- **DG-Lab Coyote 3.0 support:** the Coyote joins PiShock and OpenShock as a
  punishment device. StayPutVR runs a small WebSocket server on your PC and shows a
  QR code on the Integrations → DG-Lab tab; scanning it with the DG-Lab app (Socket
  Control) pairs the two, and your phone relays commands to the Coyote over
  Bluetooth — no dongle, driver, or third-party service required. The Coyote's two
  output channels (A and B) appear in the Devices tab as draggable green chips, so
  each tracker, the jaw constraint, or the mic constraint can drive whichever
  channel you like. Warning and disobedience pulses have their own intensity,
  duration, frequency, and waveform (steady / pulse / ramp), and the bite and OSC
  Shock triggers fire it alongside your other devices. Per-channel strength limits
  are enforced on top of the limits you set in the DG-Lab app itself, and the
  physical buttons on the Coyote still zero both channels instantly.

- **Enforced Unmute (VRChat mute):** the inverse of the microphone constraint —
  while your collar is locked, muting yourself in VRChat (via the built-in
  `MuteSelf` parameter) is punished. After a grace window, staying muted fires your
  configured disobedience actions and repeats until you unmute; unmuting at any
  point is instantly forgiven. Can be gated on the collar lock + Mic mode or left
  always armed, with an optional warning-audio nag, its own shocker/vibrator
  bindings, and a configurable cooldown. Reported on the shared `SPVR_Mic_Status`
  HUD param and configured on the Integrations → Mic tab.

### Bug fixes
- **OpenShock multi-shocker:** Bite and the OSC Shock/broadcast triggers now fire
  **all** of your configured OpenShock shockers instead of only the first one,
  matching PiShock's behavior.
- **Auto-unlock past the disable distance:** moving a locked device beyond the
  disable distance (e.g. taking a tracker off or leaving the play space) now
  auto-unlocks that specific device; other locked devices keep enforcing.
- **Mic HUD icon:** with Enforced Unmute enabled, the shared mic status icon no
  longer stays lit all the time — it only lights for the mute grace-warning and
  punishment, unless the mic-loudness monitor is active (which keeps its steady
  "monitoring" indicator).
- **Emergency stop hardening:** emergency stop now reliably suspends the Enforced
  Unmute / microphone enforcement while it is active.

## 1.4 — Bug fixes, PiShock v2 default, fewer synced params, UI overhaul (in development)

**Requires the new 1.4 avatar prefab** — reduces synced params and adds/renames OSC
parameters (`SPVR_Mic_Status`, `SPVR_Collar_Mode`, `SPVR_SoundEffect`); the
per-feature `SPVR_JawEnabled` radial is retired in favor of the unified collar toggle.

### Restraints & avatar
- **VRCFT JawOpen constraint** — with VRCFaceTracking, lock your collar and your
  jaw must stay where it was when locked (mouth held open or closed); straying too
  far escalates warning → disobedience like the position constraint. Driven by the
  `SPVR_JawOpen` bridge param; configured on the Integrations → VRCFT tab and the
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
- **Collar display self-heals** — the in-game collar-mode display no longer blanks out
  and stays stale after an avatar reload. The app re-asserts the current collar mode on
  every lock/unlock/warning/disobedience edge and again ~1s after an avatar change, so a
  param reset on avatar load is corrected automatically instead of waiting for the next
  collar toggle.
- **In-game sound effects** — on lock, unlock, warning, disobedience, and collar-mode
  switch, the app can pulse an int enum on `SPVR_SoundEffect` so an avatar animation
  layer plays a sound. Per-event toggles (on by default) under
  Settings → Notifications → "In-Game Sound Effects".
- Status tab now shows live Jaw and Mic rows alongside tracked devices, plus a
  collar-mode and mic-level readout.

### Shockers
- **PiShock warning-zone actions** — the PiShock Actions tab now has a full Warning
  Zone section (beep / vibrate / shock with their own intensity & duration), parallel
  to the Out of Bounds section.
- PiShock intensity controls (warning + disobedience, master & per-device) now have
  +/- nudge buttons that step in small increments.
- **Warnings no longer starve the disobedience shock** — warning and disobedience
  actions previously shared one rate-limit timer, so repeated warnings could prevent
  the disobedience shock from ever firing. They now throttle independently.
- PiShock warnings honored their config instead of firing a hardcoded beep + the
  *disobedience* vibrate; warnings are now silent unless explicitly configured.

### General
- **Clearer config errors & settings that actually stick** — StayPutVR now tells the
  difference between a first run (no settings yet) and a real problem reading or saving
  your settings. If the settings file can't be read or saved — typically a permissions
  leftover from a past "Run as administrator", a read-only file, or antivirus /
  Controlled Folder Access blocking the folder — a warning banner appears with step-by-step
  fixes and an "Open Config Folder" button. A corrupt settings file is moved aside
  (`config.ini.corrupt-<timestamp>`) instead of being silently overwritten, a single bad
  value no longer discards all your other settings, and saves are written atomically so a
  crash mid-save can't corrupt the file. A startup self-check logs exactly where settings
  live and whether the folder is writable, so "my settings don't save" is diagnosable from
  the log alone.
- Settings → Notifications: "Audio Notifications" renamed to **"App Sound Effects"**
  (the on-PC cues), distinct from the new in-game sound effects.
- JawOpen input callbacks are registered on startup auto-connect (not only on a manual
  OSC toggle), so the jaw value/constraint works on a normal launch.
- OSC inbound-callback registration consolidated into one place so the startup and
  reconnect paths can't drift (this also fixed per-device shock intensities not
  applying until an OSC toggle).
- Collar toggle has a time debounce so contact bounce / rapid taps don't
  multi-advance the mode.
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
