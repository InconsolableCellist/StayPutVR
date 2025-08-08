# Table of Contents

- [🔒 StayPutVR](#-stayputvr)
  - [💖 Support Development](#-support-development)
  - [📥 Quickstart](#-quickstart)
  - [✨ Features](#-features)
  - [📡 OSC Integration](#-osc-integration)
  - [📡 PiShock Integration](#-pishock-integration)
  - [💬 Twitch Integration (experimental)](#-twitch-integration-experimental)
  - [🖥️ Interface](#-interface)
  - [🔧 System Requirements](#-system-requirements)
  - [📂 Project Structure](#-project-structure)
  - [📚 Dependencies](#-dependencies)
  - [📋 Configuration](#-configuration)
  - [💾 Building From Source](#-building-from-source)
  - [🗺️ Roadmap](#-roadmap)
  - [📅 Version History](#-version-history)
  - [⚖️ License](#-license)
  - [🙏 Acknowledgments](#-acknowledgments)

# 🔒 StayPutVR

Lock yourself or your friends into positions in VR! Want your friend to keep his or her ~~paws~~ hands behind his back? No problem. Chat wants to make you stand on one foot for 60 seconds? Easy. Have to bend over and touch your toes or you lose some kind of game? It's possible, with ~~punishments~~ consequences for failing.

You can be locked via clicking a button locally, or locked remotely via OSC integration (a friend grabbing your StayPutVR-compatible cuffs, for example), at which point your movement will be locked. Once locked, if your devices move too far away from their current position there'll be conseqeuences of your chosing: an audio warning, an OSC message that drives animation on your avatar, or even external messages to OSC-compatible applications, like [PiShock](https://pishock.com/).

Cooldown timers, audio warnings, and PiShock intensity are all configurable, and the control remains completely and totally on your own computer, with you choosing who can control your avatar via VRChat's avatar interaction system.

**Compatible prefab: [foxipso.gumroad.com/l/stayputvr](https://foxipso.gumroad.com/l/stayputvr)**


## 💖 Support Development

If you enjoy using StayPutVR and want to support me, consider making a donation! Your contributions help me create content for VRChat!

[![](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=YRN6YJ5XU8Z8E)

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

- 🔐 Position locking for any and all VR tracked devices you select
- 🎯 Configurable boundary radii:
  - ✅ **Safe zone**: You're complying beautifully.
  - ⚠️ **Warning zone**: You're straying too far--watch out!
  - ❌ **Non-Compliance zone**: Now you've done it.
  - 🛑 **Disable zone**: Safety threshold for tracking errors or if you wish to stop consenting--auto unlocks and stops any output!
- 📡 Integration with VRChat, PiShock, and Twitch (experimental)
- 🔊 Audio cues for warnings and boundary violations
- ⏱️ Configurable timers for automatic unlocking
- 💬 Twitch chat bot with real-time command processing

## 🗺️ Roadmap 

* OpenShock integration
* ButtplugIO integration
* DG-Lab integration
* Placement spheres/hints (attempt #2)
* In-world audio emitters (attempt #2)

## 📅 Version History

1.0.0 - Initial release

## ⚖️ License

This project is licensed under the Apache 2.0 License. See [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- Based on the OpenVR driver example
- Special thanks to all contributors and testers 