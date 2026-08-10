# What's New in StayPutVR 1.5.1

Thanks for your support! This release teaches bites where they landed: your avatar
now reports which body part was bitten, and you can send each one to a different
shocker or toy.

As always you can get support on my Discord, and join my Patreon for supporter
recognition, to support my work, and for exclusives.

## New in 1.5.1
- **Bite zones:** your prefab now reports *where* it was bitten — tail, either ear,
  either thigh, or the jaw — and each of those can drive a different device. Open
  Devices → Visual, switch the view to **Bite zones**, and drag your shocker, DG-Lab
  channel, or BPIO chips onto the body part you want them to answer for. Each zone
  gets its own intensity and duration, so a tail nip and an ear bite don't have to
  feel the same.
- Bite zones are opt-in: tick **Route bites by body part** in Integrations → OSC
  Triggers. Until you do — and for any zone you never bound anything to — a bite
  fires everything at the usual Bite intensity, exactly as before.
- BPIO toys can now react to bites too, when you bind them to a zone.

## New in 1.5.0
- **Name your shockers:** each PiShock and OpenShock slot now has an optional name
  field next to its ID. Call one "Left ankle" and that's what you'll see everywhere
  you bind it, instead of a bare 0–4. Leave it blank to keep the old numbering.
- **Bite counter:** the OSC Triggers tab now shows how many bites you've taken this
  session and in total, with a Reset button.

Bug fixes:
- **Chaining mode kept re-locking.** VRChat re-sends avatar parameters, and every
  repeat of a still-held lock latch was treated as a new lock — which quietly moved
  each locked device's reference position to wherever it was at that moment, undid
  unlocks you made in the app, and switched jaw/mic mode back on. Lock parameters
  now only act when they actually change.
- **Safe mode showed locks that weren't real.** During emergency stop, and for a
  device that auto-unlocked past the disable distance, your cuff could turn red
  again even though nothing was being enforced.
- **Emergency stop now blocks the global out-of-bounds trigger too** — it could
  previously still fire your shockers while the safeword was active.
- **OSCQuery was burning two CPU cores** the whole time it was enabled. Fixed.

## New in 1.4.2
- **DG-Lab Coyote 3.0 support (new integration):** your Coyote can now be a
  punishment device alongside PiShock and OpenShock. Open Integrations → DG-Lab,
  tick the agreement, and scan the QR code with the DG-Lab app — your phone relays
  to the Coyote over Bluetooth, so there's nothing extra to buy or install. Both
  output channels (A and B) show up in the Devices tab as green chips you can drag
  onto any body slot, with their own intensity, duration, frequency, and waveform.
  Your phone must stay on the same network with the app open.
- **Enforced Unmute (new feature):** while your collar is locked, muting yourself
  in VRChat is now punished — after a short grace period, staying muted fires your
  shockers and repeats until you unmute (unmuting is instantly forgiven). Set it up
  on the Integrations → Mic tab, with its own shocker bindings and cooldown.

Bug fixes:
- OpenShock: Bite and the OSC Shock trigger now fire **all** your configured
  shockers, not just the first one (like PiShock already did)
- Moving a locked device far out (past the disable distance) now auto-unlocks
  that device
- With Enforced Unmute on, the mic HUD icon no longer stays lit constantly — it
  only lights for the warning and punishment
- Emergency stop now reliably stops the Enforced Unmute / mic enforcement

## Important
- **Please update your prefab to 1.4+ to properly use this release.**
  (It *should* be backwards compatible.)

## Bug fixes & integrations
- Fixed PiShock Beep + Vibrate + Shock firing, from a single event (Github Issue #9)
- Changing avatars unlocks and resets device status instead of leaving it stale (#6)
- Supports the new OSC Shock Parameter (compatible with Chillout Charles' Simple
  Shock System).
- BiteTech OSC shock support is now surfaced in the UI
- New ability to adjust the intensity/duration of bite/shock-based params (i.e., 
  you can be shocked at 10% for 1s if you move, but if you're bitten, 50% for 5s) (#7)
- OSCQuery (mDNS) support w/ manual fallback/option (#8)
- PiShock WebSocket v2 is now the default for new users
- Device status is now sent as 3 bools per device (15 synced bits) instead of
  5 synced ints (40 bits) (Thanks Rayn for the suggestion)
- Settings now always load and save from %APPDATA%\StayPutVR\config; existing
  configs are migrated automatically, and the Settings > Folders buttons now
  open the correct locations

## Interface
- Redesigned Devices tab: drag-and-drop role assignment onto an avatar effigy,
  a per-device movement heat meter, and a scaled radial zone map
- Reorganized into Status / Devices / Integrations / Settings tabs
- OSC enable and the Bite/Shock triggers in the Status tab
- All settings auto-save instantly; added an adjustable UI font scale
- Refreshed theme, and this splash + What's New screen

Questions, ideas, or bugs? Visit foxipso.com. -Foxipso
Follow me on Twitter, Patreon, Discord, etc!
