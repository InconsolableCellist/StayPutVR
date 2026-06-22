# What's New in StayPutVR 1.4

Thanks for updating! This release bundles bug fixes, fewer synced avatar
parameters, and a refreshed interface.

## Important
- This release reduces synced avatar parameters and **requires the new 1.4
  avatar prefab**. Grab it from Gumroad or Jinxxy.

## Bug fixes & integrations
- PiShock WebSocket v2 is now the default for new users
- PiShock Beep + Vibrate + Shock now all fire from a single event (#9)
- Changing avatars unlocks and resets device status instead of leaving it stale (#6)
- New OSC Shock parameter, plus the Bite shock is surfaced in the UI; both have
  their own intensity/duration and are blocked while emergency stop is active (#7)
- OSCQuery (mDNS) auto-discovery so StayPutVR no longer fights other apps over
  the OSC receive port (#8) - can be turned off to use manual ports
- Device status is now sent as 3 bools per device (15 synced bits) instead of
  5 synced ints (40 bits)

## Interface
- Redesigned Devices tab: drag-and-drop role assignment onto an avatar effigy,
  a per-device movement heat meter, and a scaled radial zone map
- Reorganized into Status / Devices / Integrations / Settings tabs
- OSC enable and the Bite/Shock triggers are reachable right from the Status tab
- Settings auto-save instantly; added an adjustable UI font scale
- Refreshed theme, and this splash + What's New screen

Questions, ideas, or bugs? Visit foxipso.com. -Foxipso
