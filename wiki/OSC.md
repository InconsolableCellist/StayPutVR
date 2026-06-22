## 📡 OSC Integration

StayPutVR integrates with VRChat and other OSC-compatible applications. The system supports both incoming commands (to control locking) and outgoing status messages (to reflect device states).

The following documentation is crucial for any avatar creator who wishes to make their own integration.

For most users, I strongly recommend using my collar & cuffs prefab, which is available for purchase on Gumroad (and which supports my work!): [foxipso.gumroad.com/l/stayputvr](https://foxipso.gumroad.com/l/stayputvr)

### Device Status Messages (Outgoing)

StayPutVR sends the current state of each tracked device to the avatar.

**Path Format**: `/avatar/parameters/SPVR_{DeviceName}_Status`
**Value Type**: Integer (0-5)

**Status Values:**
- `0`: **Disabled** - Device is disabled or unknown state
- `1`: **Unlocked** - Device is free to move (Green LED)
- `2`: **Locked Safe** - Device is locked and within safe boundaries (Red LED)
- `3`: **Locked Warning** - Device is locked and in warning zone (Flashing Yellow LED)
- `4`: **Locked Disobedience** - Device is locked and user is disobeying (Flashing Red LED)
- `5`: **Locked Out of Bounds** - Device is locked and completely out of bounds (Blinking White)

`{DeviceName}` is one of `HMD`, `ControllerLeft`, `ControllerRight`, `FootLeft`, `FootRight` (`Hip` is also supported).

#### Synced parameter footprint (avatar creators)

OSC parameters are written on the *local* client only and do **not** consume synced parameter space by themselves — synced cost is determined by which parameters your **VRChat Expression Parameters** declare as networkSynced.

- **Pre-1.4 prefab:** declares `SPVR_{DeviceName}_Status` as a **synced int** → 5 devices × 8 bits = **40 synced bits**.
- **1.4 prefab:** keeps `SPVR_{DeviceName}_Status` as a **local** (non-synced) animator parameter and decodes it, via a VRC Avatar Parameter Driver layer, into **3 synced bools** `SPVR_{DeviceName}_Status_b0/_b1/_b2` (encoding the value as `b2·4 + b1·2 + b0`). Only the bools are synced → 5 devices × 3 bits = **15 synced bits**.

Because StayPutVR sends the same `_Status` int either way, the app works unchanged with both prefabs — the optimization is entirely avatar-side. The `Foxipso → StayPutVR → Setup Controller (1.4)` editor menu sets up the decode layer + synced bools automatically.

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

