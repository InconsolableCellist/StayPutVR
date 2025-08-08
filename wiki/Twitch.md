## 💬 Twitch Integration (experimental)

StayPutVR includes Twitch integration that allows viewers to interact with your VR session through chat commands, donations, subscriptions, and bits. The integration supports both automated responses to Twitch events and real-time chat command processing.

*Because I'm not a Twitch partner I need testers for this feature!*
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