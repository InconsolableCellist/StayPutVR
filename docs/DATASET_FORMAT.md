# StayPutVR Dataset Format (spvr-dataset v1)

Raw tracker-pose capture produced by the **Dataset** tab (branch: `dataset-capture`).
One directory per recording session:

```
<datasets root>/                     default: <AppData>/StayPutVR/datasets
  2026-07-15_19-44-53/               session start, local time
    poses.bin                        binary pose stream (append-only)
    manifest.json                    session metadata (rewritten every ~15 s)
```

## Capture philosophy

**Everything is recorded raw; nothing is filtered at capture time.** Tracking
dropouts, drift, occlusion glitches, AFK periods, and left-on-overnight sessions
are all in the stream, flagged with the tracking state SteamVR reported —
they are training signal, not noise. Filtering/labeling decisions belong to
training-time consumers, where they are reversible. The only capture-time
control is **Pause** (a privacy affordance); paused intervals appear in
`manifest.json` under `pauses`.

## Timestamps

Poses are stamped **in the SteamVR driver** at the moment
`GetRawTrackedDevicePoses` was read (`sample_time_*`), not on pipe receipt —
receipt stamping would bake IPC jitter into every derived action label. Two
clocks are carried:

| Field | Clock | Use |
|---|---|---|
| `sample_time_wall` | `system_clock` (Unix seconds, f64) | cross-stream alignment (video, OSC, audio logs) |
| `sample_time_mono` | `steady_clock` (seconds, f64) | monotonic deltas, derivatives |
| `recv_wall` / `recv_mono` | app clocks at pipe receipt | latency diagnostics; fallback when the driver sends legacy v1 messages (`sample_* == 0.0`) |

## poses.bin layout

All values little-endian. File header:

| Offset | Size | Value |
|---|---|---|
| 0 | 8 | magic `"SPVRDS01"` |
| 8 | 4 | u32 format version (1) |
| 12 | 4 | u32 reserved (0) |

Then frame records, back to back, one per driver frame (~90 Hz):

```
f64  sample_time_wall     driver stamp, Unix seconds (0.0 = legacy v1 message)
f64  sample_time_mono     driver stamp, steady seconds
f64  recv_wall            app stamp at pipe receipt
f64  recv_mono            app stamp at pipe receipt
u16  device_count
device_count × device record:
  u16  device_index       -> manifest "devices" table (serial, type)
  u8   flags              bit0 = connected, bit1 = pose_valid
  u8   tracking_result    raw vr::ETrackingResult (1 = Uninitialized,
                          100/101 = Calibrating, 200 = Running_OK,
                          201 = Running_OutOfRange, ...)
  3×f32  position         meters, SteamVR world space
  4×f32  rotation         quaternion (x, y, z, w)
  3×f32  velocity         m/s, world space
  3×f32  angular_velocity rad/s, world space
```

Device record = 56 bytes; frame header = 34 bytes. A 6-device set at 90 Hz is
~33 MB/hour. The file is append-only and remains parseable after a crash
(truncate any incomplete trailing record).

## manifest.json

```jsonc
{
  "format": "spvr-dataset",
  "version": 1,
  "app_version": "1.4.1",
  "git_hash": "a54f575",
  "source": "steamvr-driver",        // or "simulator" (dev synthetic feed)
  "started_wall": 1784166293.23,     // Unix seconds
  "started_iso": "2026-07-15 19:44:53",
  "updated_wall": 1784166297.23,     // last periodic rewrite
  "ended_wall": 1784166297.23,       // absent if the app crashed mid-session
  "clean_shutdown": true,            // false => counters may lag by ~15 s
  "frames": 266,
  "samples": 1596,
  "bytes": 98436,
  "frames_dropped": 0,               // ring-buffer backstop drops (disk stall)
  "paused_seconds": 1.0,
  "pauses": [ { "start_wall": ..., "end_wall": ... } ],
  "devices": [
    { "index": 0, "serial": "LHR-XXXXXXXX", "type": "HMD" },
    { "index": 3, "serial": "LHR-YYYYYYYY", "type": "Tracker" }
  ]
}
```

The manifest is rewritten atomically (temp file + rename) every ~15 s during
recording, so a crash loses at most that much bookkeeping; the pose stream
itself is flushed every ~5 s.

## Reading it (Python sketch)

```python
import json, struct
from pathlib import Path

def read_session(session_dir):
    manifest = json.loads((Path(session_dir) / "manifest.json").read_text())
    devices = {d["index"]: d for d in manifest["devices"]}
    frames = []
    with open(Path(session_dir) / "poses.bin", "rb") as f:
        assert f.read(8) == b"SPVRDS01"
        version, _reserved = struct.unpack("<II", f.read(8))
        while True:
            head = f.read(34)
            if len(head) < 34: break
            sw, sm, rw, rm, n = struct.unpack("<ddddH", head)
            samples = []
            for _ in range(n):
                rec = f.read(56)
                if len(rec) < 56: return manifest, frames  # truncated tail
                idx, flags, result, *vals = struct.unpack("<HBB13f", rec)
                samples.append({
                    "device": devices[idx]["serial"],
                    "connected": bool(flags & 1),
                    "pose_valid": bool(flags & 2),
                    "tracking_result": result,
                    "pos": vals[0:3], "quat": vals[3:7],
                    "vel": vals[7:10], "angvel": vals[10:13],
                })
            frames.append({"t_wall": sw, "t_mono": sm, "samples": samples})
    return manifest, frames
```

## Wire protocol note (driver ↔ app)

The SteamVR driver now emits `DEVICE_UPDATE_V2` (msgType `3`) on the
`\\.\pipe\StayPutVR` named pipe, adding the batch timestamps, velocities,
validity flags, and tracking result to the v1 layout. The app still parses
legacy v1 (msgType `1`) — a mismatched old driver keeps working, with
`sample_time_* = 0.0` and receipt-time stamps as the fallback. Producer:
`driver/IPC/IPCServer.cpp` (`SendDeviceUpdates`); consumer:
`application/src/IPC/IPCClient.cpp` (`ProcessDeviceUpdateMessageV2`).
