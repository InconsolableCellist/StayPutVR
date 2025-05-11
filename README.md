# 🔒 StayPutVR

Lock yourself or your friends into positions in VR! Want your friend to keep his or her paws behind his back? No problem. Chat wants to make you stand on one foot for 60 seconds? Easy. Have to bend over and touch your toes or you lose some kind of game? It's possible, with ~~punishments~~ consequences for failing.

You can be locked via clicking a button locally, or locked remotely via OSC integration (a friend grabbing your StayPutVR-compatible cuffs, for example), at which point your movement will be locked. Once locked, if your devices move too far away from their current position there'll be conseqeuences of your chosing: an audio warning, an OSC message that drives animation on your avatar, or even external messages to OSC-compatible applications, like [ButtplugIO](https://github.com/buttplugio) or [PiShock](https://pishock.com/).

Cooldown timers, audio warnings, and a safeword override are all configurable, and the control remains completely and totally on your own computer--no servers or external calls, other than the OSC integration you setup.

## 📥 Downloading/Installation

StayPutVR is FOSS and created by Foxipso. You can download the code in this Github repo and use the project for free!

However, for: precompiled binaries; VRChat-compatible prefabs; a detailed usage guide and video; and to support the creator, you can visit my Gumroad:

[foxipso.gumroad.com](https://foxipso.gumroad.com/l/stayputvr)

Not only does this support me and my work (and is greatly appreciated!) this is also the easiest way to integrate StayPutVR with your avatar in VRChat. The prefab also comes with compatible cuffs and a collar, which can be grabbed by your friends to control StayPutVR.

## ✨ Features

- 🔐 Position locking for any and all VR tracked devices you select
- 🎯 Configurable boundary radii:
  - ✅ **Safe zone**: You're complying beautifully.
  - ⚠️ **Warning zone**: You're straying too far--watch out!
  - ❌ **Non-Compliance zone**: Now you've done it.
  - 🛑 **Disable zone**: Safety threshold for tracking errors or if you wish to stop consenting--auto unlocks and stops any output!
- 📡 OSC integration with VRChat and apps like ButtplugIO and PiShock
- 🔊 Audio cues for warnings and boundary violations
- 📳 Haptic feedback for supported devices
- ⏱️ Configurable timers for automatic unlocking
- 🔄 Chaining mode to lock all devices when one device is locked, or lock/unlock each one individually

## 🖥️ Interface

StayPutVR is a desktop application that registers with SteamVR

- **Main Tab**: Primary control panel with status indicators and quick actions
- **Devices Tab**: Detailed device management and individual control
- **Boundaries Tab**: Configure zones with visual representation
- **Notifications Tab**: Audio and haptic feedback settings
- **Timers Tab**: Configure cooldown timers and hold-pose timers
- **OSC Tab**: OSC connection settings and message configuration
- **Settings Tab**: Application settings and configuration management

## 📡 OSC Configuration

StayPutVR can receive and send OSC messages to communicate with external applications like VRChat. This allows for:

- Locking device positions via OSC commands
- Sending notifications when positions exceed thresholds
- Triggering external events based on position status

## 🔧 System Requirements

- Windows 10 or higher
- SteamVR
- OpenVR SDK 2.5.1 or compatible
- Visual Studio 2019 or higher (for building)
- CMake 3.15 or higher (for building)

## 🚀 Quick Start

1. Build the project using CMake
2. Install the driver using `cmake --build . --target install`
3. Register the driver using vrpathreg
4. Launch SteamVR
5. Use the main interface to lock positions and configure boundaries

For detailed instructions, see [INSTALLATION.md](INSTALLATION.md).

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

## 📅 Version History

1.0.0 - Initial release with tabbed UI and core functionality

## ⚖️ License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🙏 Acknowledgments

- Based on the OpenVR driver example
- Special thanks to all contributors and testers 