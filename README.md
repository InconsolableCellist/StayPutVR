# 🔒 StayPutVR

Lock yourself or your friends into positions in VR! Want your friend to keep his or her paws behind his back? No problem. Chat wants to make you stand on one foot for 60 seconds? Easy. Have to bend over and touch your toes or you lose some kind of game? It's possible, with ~~punishments~~ consequences for failing.

You can be locked via clicking a button locally, or locked remotely via OSC integration (a friend grabbing your StayPutVR-compatible cuffs, for example), at which point your movement will be locked. Once locked, if your devices move too far away from their current position there'll be conseqeuences of your chosing: an audio warning, an OSC message that drives animation on your avatar, or even external messages to OSC-compatible applications, like [PiShock](https://pishock.com/).

Cooldown timers, audio warnings, and PiShock intensity are all configurable, and the control remains completely and totally on your own computer, with you choosing who can control your avatar via VRChat's avatar interaction system.

## 🎥 Video

<iframe width="560" height="315" src="https://www.youtube.com/embed/dQw4w9WgXcQ?si=1234567890" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>

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
6. Configure the countdown timer, safe zones, and other settings like PiShock if you wish.
7. Lock individual devices on the Devices Tab, or set devices to "Will Lock" and then click "Lock All Included Devices" to lock them all at the same time!
8. Move your devices and you should get a warning if you're out of bounds, or a shock if you're out of bounds and the device is set to shock!

In VRChat, you can now use my public test avatar (Foxipso Base), add it to your own avatars using [my prefab](https://foxipso.gumroad.com/l/stayputvr), or make your own custom objects that utilize the OSC integration.

## ✨ Features

- 🔐 Position locking for any and all VR tracked devices you select
- 🎯 Configurable boundary radii:
  - ✅ **Safe zone**: You're complying beautifully.
  - ⚠️ **Warning zone**: You're straying too far--watch out!
  - ❌ **Non-Compliance zone**: Now you've done it.
  - 🛑 **Disable zone**: Safety threshold for tracking errors or if you wish to stop consenting--auto unlocks and stops any output!
- 📡 Integration with VRChat and PiShock
- 🔊 Audio cues for warnings and boundary violations
- ⏱️ Configurable timers for automatic unlocking

## 📡 OSC Integration

StayPutVR integrates with VRChat and other OSC-compatible applications. The system supports both incoming commands (to control locking) and outgoing status messages (to reflect device states).

The following documentation is crucial for any avatar creator who wishes to make their own integration.

For most users, I strongly recommend using my collar & cuffs prefab, which is available for purchase on Gumroad (and which supports my work!): [foxipso.gumroad.com/l/stayputvr](https://foxipso.gumroad.com/l/stayputvr)

### Device Status Messages (Outgoing)

StayPutVR sends device status updates to indicate the current state of each tracked device:

**Path Format**: `/avatar/parameters/SPVR_{DeviceName}_Status`  
**Value Type**: Integer (0-5)

**Status Values:**
- `0`: **Disabled** - Device is disabled or unknown state
- `1`: **Unlocked** - Device is free to move (Green LED)
- `2`: **Locked Safe** - Device is locked and within safe boundaries (Red LED)
- `3`: **Locked Warning** - Device is locked and in warning zone (Flashing Yellow LED)
- `4`: **Locked Disobedience** - Device is locked and user is disobeying (Flashing Red LED)
- `5`: **Locked Out of Bounds** - Device is locked and completely out of bounds (Blinking White)

### Device Lock Control (Incoming)

Control individual device locking states via OSC:

**Default Paths:**
- `/avatar/parameters/SPVR_HMD_Latch_IsPosed`
- `/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed`
- `/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed`
- `/avatar/parameters/SPVR_FootLeft_Latch_IsPosed`
- `/avatar/parameters/SPVR_FootRight_Latch_IsPosed`
- `/avatar/parameters/SPVR_Hip_Latch_IsPosed`

**Value Type**: Boolean (true/1 = lock, false/0 = unlock)

### Device Include Control (Incoming)

Toggle whether devices are included in global locking operations:

**Default Paths:**
- `/avatar/parameters/SPVR_HMD_include`
- `/avatar/parameters/SPVR_ControllerLeft_include`
- `/avatar/parameters/SPVR_ControllerRight_include`
- `/avatar/parameters/SPVR_FootLeft_include`
- `/avatar/parameters/SPVR_FootRight_include`
- `/avatar/parameters/SPVR_Hip_include`

**Value Type**: Boolean (true/1 = toggle include state)  
**Behavior**: Sending `true` toggles the device's "Include in Locking" setting

### Global Lock Controls (Incoming)

Control all devices simultaneously:

**Default Paths:**
- `/avatar/parameters/SPVR_Global_Lock` - Lock all included devices
- `/avatar/parameters/SPVR_Global_Unlock` - Unlock all devices

**Value Type**: Boolean (true/1 = activate)

### Supported Device Types

StayPutVR recognizes and can control the following device types:
- **HMD** - Head-mounted display
- **ControllerLeft** - Left hand controller
- **ControllerRight** - Right hand controller  
- **FootLeft** - Left foot tracker
- **FootRight** - Right foot tracker
- **Hip** - Hip/waist tracker

### OSC Configuration

Default OSC settings:
- **Send Port**: 9000 (to VRChat/applications)
- **Receive Port**: 9001 (from VRChat/applications)  
- **Address**: 127.0.0.1 (localhost)

All OSC paths are configurable through the OSC tab in the application interface.


## 📡 PiShock Integration

StayPutVR can send commands to PiShock through its web interface. You just need to provide your username, API key, and share code, available via PiShock's website.

You can configure whether your device beeps, vibrates, or shocks, how long it does so for, how intense it is, and whether it repeats or is one-time only.

## 🖥️ Interface

StayPutVR is a desktop application that registers with SteamVR

- **Main Tab**: Primary control panel with status indicators and quick actions
- **Devices Tab**: Detailed device management and individual control
- **Boundaries Tab**: Configure zones with visual representation
- **Notifications Tab**: Audio and haptic feedback settings
- **Timers Tab**: Configure cooldown timers and hold-pose timers
- **OSC Tab**: OSC connection settings and message configuration
- **Settings Tab**: Application settings and configuration management

## 🔧 System Requirements

- Windows 10 or higher
- SteamVR
- OpenVR SDK 2.5.1 or compatible
- Visual Studio 2019 or higher (for building)
- CMake 3.15 or higher (for building)

## 📂 Project Structure

- `src/Driver/` - OpenVR driver implementation
- `src/UI/` - User interface implementation using Dear ImGui
  - Main Tab: Primary interface with current status and controls
  - Devices Tab: Device tracking configuration
  - Boundaries Tab: Zone configuration settings
  - Notifications Tab: Audio, haptic, and OSC notification settings
  - Timers Tab: Timer configuration for locking/unlocking
  - OSC Tab: OSC input/output configuration
- `src/Native/` - Native driver factory implementation
- `thirdparty/` - Third-party dependencies

## 📚 Dependencies

- Dear ImGui - Immediate mode GUI library
- GLFW - Multi-platform window creation library
- GLAD - OpenGL loading library
- nlohmann/json - JSON library for configuration

## 📋 Configuration

StayPutVR automatically saves and loads configurations in JSON format, allowing you to:
- Save different position setups
- Configure threshold distances for warning and lock zones
- Set up audio and haptic feedback preferences
- Configure OSC parameters

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

## 💖 Support Development

If you enjoy using StayPutVR and want to support me, consider making a donation! Your contributions help me create content for VRChat!

[![](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=YRN6YJ5XU8Z8E)

## 📅 Version History

1.0.0 - Initial release

## ⚖️ License

This project is licensed under the Apache 2.0 License. See [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- Based on the OpenVR driver example
- Special thanks to all contributors and testers 