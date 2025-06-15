# 🔒 StayPutVR

Lock yourself or your friends into positions in VR! Want your friend to keep his or her ~~paws~~ hands behind his back? No problem. Chat wants to make you stand on one foot for 60 seconds? Easy. Have to bend over and touch your toes or you lose some kind of game? It's possible, with ~~punishments~~ consequences for failing.

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
6. For Twitch integration, go to the Twitch tab, set up your Twitch application credentials, and configure chat commands.
7. Configure the countdown timer, safe zones, and other settings like PiShock if you wish.
8. Lock individual devices on the Devices Tab, or set devices to "Will Lock" and then click "Lock All Included Devices" to lock them all at the same time!
9. Move your devices and you should get a warning if you're out of bounds, or a shock if you're out of bounds and the device is set to shock!

In VRChat, you can now use my public test avatar (Foxipso Base), add it to your own avatars using [my prefab](https://foxipso.gumroad.com/l/stayputvr), or make your own custom objects that utilize the OSC integration.

## ✨ Features

- 🔐 Position locking for any and all VR tracked devices you select
- 🎯 Configurable boundary radii:
  - ✅ **Safe zone**: You're complying beautifully.
  - ⚠️ **Warning zone**: You're straying too far--watch out!
  - ❌ **Non-Compliance zone**: Now you've done it.
  - 🛑 **Disable zone**: Safety threshold for tracking errors or if you wish to stop consenting--auto unlocks and stops any output!
- 📡 Integration with VRChat, PiShock, and Twitch
- 🔊 Audio cues for warnings and boundary violations
- ⏱️ Configurable timers for automatic unlocking
- 💬 Twitch chat bot with real-time command processing

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

## 💬 Twitch Integration

StayPutVR includes comprehensive Twitch integration that allows viewers to interact with your VR session through chat commands, donations, subscriptions, and bits. The integration supports both automated responses to Twitch events and real-time chat command processing.

### 🚀 Quick Setup

1. **Create a Twitch Application**:
   - Go to [dev.twitch.tv/console](https://dev.twitch.tv/console)
   - Click "Register Your Application"
   - Name: `StayPutVR` (or your preferred name)
   - OAuth Redirect URLs: `http://localhost:8080/auth/twitch/callback`
   - Category: `Game Integration`
   - Copy your **Client ID** and **Client Secret**

2. **Configure StayPutVR**:
   - Open the **Twitch tab** in StayPutVR
   - Enter your **Client ID** and **Client Secret**
   - Enter your **Twitch channel name** (your username)
   - Configure your **bot username** (can be the same as channel name)
   - Click **"Test OAuth Flow"** to authorize the application

3. **Set Up Chat Commands**:
   - Configure your **command prefix** (default: `!`)
   - Set your **lock command** (default: `lock`)
   - Set your **unlock command** (default: `unlock`) 
   - Set your **status command** (default: `status`)
   - Enable **"Enable Chat Commands"** checkbox

### 💬 Chat Commands

Once configured, viewers can use these commands in your Twitch chat:

- **`!lock`** - Locks all devices marked as "Will Lock" in the Devices tab
  - Equivalent to clicking "Lock All Included Devices" button
  - Plays lock sound and sends OSC status updates
  - Responds with confirmation message in chat

- **`!unlock`** - Unlocks all locked devices
  - Equivalent to clicking "Unlock All Included Devices" button  
  - Plays unlock sound and sends OSC status updates
  - Responds with confirmation message in chat

- **`!status`** - Reports current device and lock status
  - Shows total devices detected
  - Shows how many devices are included in locking
  - Reports current global lock state
  - Shows individual device lock counts

**Example Chat Interaction:**
```
Viewer: !lock
StayPutVRBot: @Viewer Locking devices!

Viewer: !status  
StayPutVRBot: @Viewer StayPutVR Status: 6 devices detected, 4 included in locking, GLOBAL LOCK ACTIVE (4 devices locked)

Viewer: !unlock
StayPutVRBot: @Viewer Unlocking devices!
```

### 🎁 Event-Based Automation

StayPutVR can automatically respond to Twitch events:

> **TODO:** This feature, particularly the bits and donations functionality, needs to be tested with a Twitch Partner. If you are a Twitch Partner and have access to bits and donations, your help in testing would be greatly appreciated!



#### Donations & Bits
- **Minimum thresholds** - Set minimum donation/bit amounts to trigger locks
- **Dynamic duration** - Lock duration scales with donation amount
- **Targeting options** - Lock all devices or specific device types
- **Chat responses** - Automatic thank you messages with lock details

#### Subscriptions & Gift Subs
- **Auto-lock on sub** - New subscriptions trigger device locks
- **Gift sub bonuses** - Gift subscriptions can have longer lock times
- **Celebration messages** - Automatic chat responses for new subs

### ⚙️ Configuration Options

**Connection Settings:**
- **Client ID** - Your Twitch application client ID
- **Client Secret** - Your Twitch application client secret  
- **Channel Name** - Your Twitch channel username
- **Bot Username** - Username for the chat bot (can be same as channel)

**Chat Bot Settings:**
- **Enable Chat Commands** - Toggle chat command processing
- **Command Prefix** - Character(s) that prefix commands (default: `!`)
- **Lock Command** - Command word for locking (default: `lock`)
- **Unlock Command** - Command word for unlocking (default: `unlock`)
- **Status Command** - Command word for status (default: `status`)

**Event Automation:**
- **Enable Donations** - Respond to donation events
- **Enable Bits** - Respond to bit events  
- **Enable Subscriptions** - Respond to subscription events
- **Minimum Amounts** - Set thresholds for triggering locks
- **Duration Settings** - Configure base duration and scaling
- **Device Targeting** - Choose which devices to lock automatically

### 🔐 Security & Privacy

- **Local OAuth Server** - Authorization handled locally on your machine
- **Secure Token Storage** - Access tokens encrypted and stored locally
- **Automatic Refresh** - Tokens refreshed automatically when needed
- **Minimal Permissions** - Only requests necessary Twitch scopes:
  - `chat:read` - Read chat messages for commands
  - `chat:edit` - Send chat responses
  - `user:read:chat` - Modern chat API access
  - `user:write:chat` - Modern chat API sending
  - `bits:read` - Receive bit events
  - `channel:read:subscriptions` - Receive subscription events

### 🛠️ Technical Details

**IRC Chat Integration:**
- Real-time IRC connection to `irc.chat.twitch.tv`
- Handles Twitch message tags and metadata
- Automatic reconnection on connection loss
- PING/PONG keepalive handling

**API Integration:**
- Uses Twitch Helix API for outbound messages
- EventSub integration for real-time events
- Rate limiting compliance (20 messages per 30 seconds)
- Automatic token validation and refresh

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
- **Twitch Tab**: Twitch integration, chat commands, and event automation
- **Settings Tab**: Application settings and configuration management

## 🔧 System Requirements

- Windows 10 or higher
- SteamVR
- OpenVR SDK 2.5.1 or compatible
- Microsoft Visual C++ Redistributable for Visual Studio 2019 or higher
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