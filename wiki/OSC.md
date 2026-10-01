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
- `/avatar/parameters/SPVR_Global_OutOfBounds` - Run the out-of-bounds actions on
  all included devices (the same actions a real boundary violation triggers,
  shocks included). Must be enabled in Settings → OSC.

**Value Type**: Boolean (true/1 = activate)

### Bite Triggers (Incoming)

A bite fires a direct shock on your configured devices (VRC BiteTech and similar
prefabs send these).

**Default Paths:**
- `/avatar/parameters/SPVR_Bite` - bite, body part unspecified
- `/avatar/parameters/SPVR_Bite_Tail`
- `/avatar/parameters/SPVR_Bite_Ear_Left`
- `/avatar/parameters/SPVR_Bite_Ear_Right`
- `/avatar/parameters/SPVR_Bite_Thigh_Left`
- `/avatar/parameters/SPVR_Bite_Thigh_Right`
- `/avatar/parameters/SPVR_Bite_Jaw`

**Value Type**: Boolean (true/1 = bitten)

The body-part parameters are always the configured bite path plus a fixed
suffix, so renaming the bite path in Settings → OSC renames the whole family.

**Routing (1.5.1)**: with *Route bites by body part* off (the default), any of
these fires every configured shocker at the Bite intensity/duration in
Integrations → OSC Triggers. With it on, a bite fires only the shockers and BPIO
toys bound to that body part — bound on the Devices tab, Visual view, "Bite
zones" — at that zone's own intensity/duration. A zone with nothing bound falls
back to firing everything.

#### Contact tags are not OSC parameters

A common point of confusion: biter assets (spray bottles, mouths, toys) carry a
`VRCContactSender` with a tag such as `BT_Bite`. **StayPutVR never sees that
tag.** Contact senders and receivers talk to each other inside VRChat's physics
world and emit no OSC at all. The chain is:

1. The biting asset's contact **sender** broadcasts its tag (e.g. `BT_Bite`).
2. A contact **receiver** on *your* avatar (the VRC BiteTech prefab) is listening
   for that tag and sets an animator parameter when it collides.
3. That prefab drives the expression parameter `SPVR_Bite` (or one of the
   body-part variants) — directly or via a VRC Avatar Parameter Driver.
4. VRChat sends `/avatar/parameters/SPVR_Bite` out over OSC, and StayPutVR
   shocks.

So the tag name and the OSC parameter name are independent, and only step 4
concerns StayPutVR. If bites aren't registering, the break is almost always at
step 3 — the receiver is firing but nothing is driving `SPVR_Bite`. Matching is
exact: only the configured bite path plus one of the six fixed suffixes is
recognised, so pointing an asset at `/avatar/parameters/BT_Bite` does nothing
unless you also change the bite path in Settings → OSC.

### Shock Trigger (Incoming)

A direct shock on your configured devices, for prefabs and companion apps that
want to fire a shock without a bite (Simple Shock System, the Dungeons of
Eternity mod).

**Default Path:** `/avatar/parameters/SPVR_Shock` (1.5.4; it was
`/avatar/parameters/Shock` before)

The old `/avatar/parameters/Shock` is still accepted alongside it while *Also
accept /avatar/parameters/Shock* is on in Settings → OSC (the default), since
apps like the Dungeons of Eternity mod send it straight to StayPutVR. On an
avatar the path changed because VRChat doesn't send back out a parameter it
received over OSC: an avatar that receives `Shock` copies it to `SPVR_Shock`
(copy the float, not just a bool, to keep the magnitude). If your avatar drives
`Shock` itself *and* copies it to `SPVR_Shock`, turn the legacy option off or
each hit fires twice. Configs still on the old default move to `SPVR_Shock` on
first launch of 1.5.4; a path you set yourself is left alone.

**Value Type**: Boolean or Float

- **Boolean** `true` fires the plain shock at the Shock intensity/duration on
  Integrations → OSC Triggers, or at each device's disobedience intensity with
  *Use per-device disobedience intensities* on.
- **Float** `0..1` (1.5.2) says how hard: the shock fires at that fraction of
  **Shock max intensity**, anywhere from 0 up to it; the bool intensity plays no
  part. With per-device intensities on, each PiShock and OpenShock device has
  its own **Shock max** in its tab; DG-Lab and the PiShock legacy API use the
  global one. A float of `0` is the release and fires nothing.

### Spank Trigger (Incoming)

A spank that comes a step harder each time, for an avatar contact that only
trips on a fast-moving hand.

**Default Path:** `/avatar/parameters/SPVR_Spank`

**Value Type**: Boolean (true/1 = spanked; StayPutVR acts on the false → true
change, so a held contact counts once)

Set it up on Integrations → Spank:

- **Fires on**: any of PiShock, OpenShock, DG-Lab and BPIO, and whether
  PiShock/OpenShock shock or vibrate. DG-Lab always pulses and BPIO always
  vibrates.
- **Intensity ladder**: the first spank fires at *Min intensity*, each after it
  *Step per spank* higher, up to *Max intensity*. At max it stays there, or with
  *Wrap back to min after max* the next spank starts over at min. Every step fires
  for the same *Duration*.
- **Timing**: spanks closer together than *Debounce* (2.5s) count once.
  *Hold before ramp-down* (5s) after the last spank, the level glides back to min
  over *Ramp-down time* (3s). A spank on the way down climbs from wherever the
  level has got to and starts the hold again; once the level is all the way down
  the next spank starts at min.

PiShock refuses a second action within 2s and OpenShock within 1s, so with a
debounce under about 2.5s some spanks climb the ladder without firing PiShock.
Blocked while emergency stop is active, like Bite and Shock.

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

