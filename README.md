- [StayPutVR](#-stayputvr)
  - [Quickstart](#-quickstart)
  - [Features](#-features)
  - [️Roadmap](#-roadmap)
  - [Building From Source](#-building-from-source)
  - [Version History](#-version-history)
  - [️License](#-license)
  - [Acknowledgments](#-acknowledgments)

# 🔒 StayPutVR

<img src="https://github.com/InconsolableCellist/StayPutVR/blob/master/logo.png" alt="StayPutVR Logo" width="300">

Lock yourself or your friends into positions in VR! Want your friend to keep his or her ~~paws~~ hands behind his back? No problem. Chat wants to make you stand on one foot for 60 seconds? Easy. Have to bend over and touch your toes or you lose some kind of game? It's possible, with ~~punishments~~ consequences for failing.

You can be locked via clicking a button locally, or locked remotely via OSC integration (a friend grabbing your StayPutVR-compatible cuffs, for example), at which point your movement will be locked. Once locked, if your devices move too far away from their current position there'll be conseqeuences of your chosing: an audio warning, an OSC message that drives animation on your avatar, or even external messages to OSC-compatible applications, like [PiShock](https://pishock.com/).

Cooldown timers, audio warnings, and PiShock intensity are all configurable, and the control remains completely and totally on your own computer, with you choosing who can control your avatar via VRChat's avatar interaction system.

**Compatible prefabs:**

Gumroad: **[foxipso.gumroad.com/l/stayputvr](https://foxipso.gumroad.com/l/stayputvr)**

Jinxxy: **[jinxy.com/foxipso/StayPutVR](https://jinxxy.com/foxipso/StayPutVR)**

Use code `github` for 10% off!

## 📥 Quickstart

StayPutVR is open-source and created by Foxipso. You can download the code in this Github repo and use the application for free!

You then need a compatible avatar. You can use my public test avatar (Foxipso Base) or add it to your own.

The easiest way to add it is to use my 3D cuffs and collar, which comes complete with a HUD and poseable locks that your friends can set. Using my prefab also supports me and my work!

[foxipso.gumroad.com/l/stayputvr](https://foxipso.gumroad.com/l/stayputvr)

1. Install the application
2. Start SteamVR and StayPutVR
3. Make sure you see "Connected to Driver"
4. Go to the Devices tab. Move each device to identify it and assign a role using the drop-down. 
5. If you wish to integrate with VRChat or another OSC application, go to the OSC tab, verify the settings, and click Enable OSC.
6. **Experimental** [*I need testers*]: For Twitch integration, go to the Twitch tab, set up your Twitch application credentials, and configure chat commands.
7. Configure the countdown timer, safe zones, and other settings like PiShock if you wish.
8. For VRChat, simply lock your cuffs or collar. For manual locking: lock individual devices on the Devices Tab, or set devices to "Will Lock" and then click "Lock All Included Devices" to lock them all at the same time!
9. Move your devices and you should get a warning if you're out of bounds, or a shock if you're out of bounds and the device is set to shock!

In VRChat, you can now use my public test avatar (Foxipso Base), add it to your own avatars using [my prefab](https://foxipso.gumroad.com/l/stayputvr), or make your own custom objects that utilize the OSC integration.

## ✨ Features

- Position locking for any and all VR tracked devices you select
- Configurable boundary radii:
  - **Safe zone**: You're complying beautifully.
  - **Warning zone**: You're straying too far--watch out!
  - **Non-Compliance zone**: Now you've done it.
  - **Disable zone**: Safety threshold for tracking errors or if you wish to stop consenting--auto unlocks and stops any output!
- Integration with VRChat, PiShock, OpenShock, BPIO and Twitch (experimental)
- Integration with Sacred's [VRCBiteTech](https://jinxxy.com/Sacred/VRCBiteTech) (get shocked when bitten!)
- Audio cues for warnings and boundary violations
- Configurable timers for automatic unlocking and shock cooldown
- Multi-shocker support (OpenShock & PiShock)
- Supports placement spheres in the prefab
- Can stop your locomotion in VRChat when locked (recommended with placement spheres)
- Emergency stop mode
  - Immediately unlocks all devices and prevents any further shocking 
  - Requires a button push in the UI to go back to normal operation
  - Can also be activated via an OSC stretch param (>0.5). Integrates with the StayPutVR prefab for a tag you can yank quickly

## 📚 Documentation

View the [wiki](https://github.com/InconsolableCellist/StayPutVR/wiki) for more information.

## 🗺️ Roadmap 

* ~~OpenShock integration~~ Done!
* ~~Multiple shockers (OpenShock)~~ Done!
* ~~Multiple shockers (PiShock)~~ Done!
* ~~PiShock WebSocket v2 support~~ Done!
* ~~Emergency stop support~~ Done!
* ~~BPIO integration~~ Done!
* DG-Lab integration
* ~~Placement spheres/hints (attempt #2)~~ Done!
* In-world audio emitters (attempt #2)

## 💾 Building From Source

### Prerequisites
- Visual Studio 2019 or higher with C++ desktop development workload
- CMake 3.15 or higher

### Build Steps
1. Clone the repository
2. Open a command prompt in the project directory
3. Run `cmake -B build`
4. Run `cmake --build build --config Release`
5. Install with `cmake --build build --target install --config Release`

## 📅 Version History

**1.4** - Bug fixes, PiShock v2 default, fewer synced params, UI overhaul (6/22/2026)
- PiShock WebSocket v2 is now the default for new users (existing users keep their saved setting)
- Fixed PiShock multi-action: Beep + Vibrate + Shock now all fire from a single event (#9)
- Changing avatars now unlocks and resets all device status instead of leaving it stale (#6)
- Added an OSC `Shock` parameter and surfaced the Bite shock in the UI — both have their own intensity/duration and are blocked while emergency stop is active (#7)
- Added OSCQuery (mDNS) auto-discovery so StayPutVR no longer fights other apps over the OSC receive port (#8); can be turned off to use manual ports
- Reduced synced avatar parameters: device status is now sent as 3 bools per device (15 synced bits) instead of 5 synced ints (40 bits) — **requires the new 1.4 avatar prefab**
- Redesigned the Devices tab: drag-and-drop role assignment onto an avatar effigy, a per-device movement "heat" meter for identifying trackers, and a scaled radial zone map (a classic list view is still available)
- Reorganized the interface into Status / Devices / Integrations / Settings tabs; OSC enable and Bite/Shock triggers are now reachable from the Status tab without digging into OSC settings
- OSC is now enabled by default, with the advanced path settings collapsed behind clearer defaults and per-section reset buttons
- Settings now auto-save instantly (no more "Save All Settings" button) and include an adjustable UI font scale
- Fixed config location: settings now always load and save from `%APPDATA%\StayPutVR\config` (existing configs are migrated automatically), and the Settings > Folders buttons now open the correct config/log/resources folders
- Refreshed the application theme
- Updated Dear ImGui
- Added a Linux development build (GUI + OSC simulation, no SteamVR driver) for avatar prefab iteration

**1.3.2** - PiShock duration fix (5/21/2026)
- Fixed bug where PiShock warning/shock duration of 1.0s was resetting to 15s on restart

**1.3.1** - Tech debt sprint (4/13/2026)
- Internal: guarded Twitch OAuth tokens with a mutex
- Internal: fixed runtime hazards, extracted UI panels, added base classes

**1.3.0** - BPIO integration (11/3/2025)
- Added BPIO integration (experimental)

**1.2.0** - PiShock WebSocket v2 support (10/27/2025)
- Added PiShock WebSocket v2 support (faster response times, future multi-device support)
- Added support for multiple shockers using PiShock WebSocket v2
- Fixed a bug where warning OSC messages ("Locked Warning", enum 3) weren't being sent

**1.1.1** - Bug fixes (10/12/2025)
- Fixed bug where StayPutVR driver wouldn't load when launching SteamVR via SteamLink or Pico Connect
- Fixed bug where PiShock warning and disobedience durations were shown as 0.0-1.0 units instead of 1.0-15.0 seconds
- Added shock cooldown timer for both PiShock and OpenShock

**1.1.0** - Multi-shocker support (9/21/2025)
- Adds support for multiple shockers using OpenShock
- Adds emergency stop mode (OSC message)

**1.0.3** - OpenShock Support (Beta) (8/25/2025)
- **OpenShock Integration**: OpenShock support equivalent to PiShock
- Fixed a bug where the default OSC receive port was 9005 instead of 9001 (VRChat default)

**1.0.2** - BiteTech support (8/24/2025)
- **BiteTech Support**: Added support for BiteTech devices, including bite detection and disobedience actions.

**1.0.1** - Performance & Stability Release (8/23/2025)
- **VR Performance Improvements**: Eliminated potential blocking operations in VR driver main loop
- **SteamVR Crash Fix**: Resolved `VRApplicationError_IPCFailed` crashes caused by IPC timeouts (after headset reconnects)

1.0.0 - Initial release (8/8/2025)

## ⚖️ License

This project is licensed under the Apache 2.0 License. See [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- Based on the OpenVR driver example
- Special thanks to all contributors and testers 