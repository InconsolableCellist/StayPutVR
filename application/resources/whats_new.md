# What's New in StayPutVR 1.4

Thanks for your support! This updated version greatly reduces the number of
required synced parameters, has lots of bugfixes, a new UI, a better device-
assignment section, and more.

As always you can get support on my Discord, and join my Patreon for supporter
recognition, to support my work, and for exclusives.

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
