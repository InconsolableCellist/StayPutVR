# StayPutVR Tech Debt Sprint 1

> **Branch:** `tech-debt/sprint-1`
> **Build platform:** Windows only (MSVC + OpenVR SDK + WinSock). The codebase has no Linux build path today, so all build + runtime verification must happen on a Windows machine. Do not attempt to port the build to Linux as part of this sprint.

## How to use this document

1. Each step is small and self-contained. Implement one step, build, run the listed manual tests, confirm no regression, then move on.
2. The **Progress log** at the bottom records what has been merged and what is still pending verification.
3. If a build breaks, stop and fix before the next step. Never stack two unverified steps.
4. If a manual test regresses, revert the step's commit and investigate before continuing.

---

## Context

StayPutVR is a C++ SteamVR driver + application that locks VR devices in place and integrates with PiShock, OpenShock, Buttplug (BPIO), Twitch, and OSC/VRChat. The codebase has grown feature-by-feature since 1.0.0 (Aug 2025) through 1.3.0 (Nov 2025), adding five external integrations. A revisit audit reveals debt concentrated in a handful of god files plus real thread-safety hazards in worker threads.

**Total source (excluding thirdparty):** ~16,245 LoC. Top offenders:
- `application/src/ui/UIManager.cpp` — **6025 lines** (god class: rendering + state + config I/O + timers + 5 manager owners)
- `application/src/managers/TwitchManager.cpp` — **1834 lines** (OAuth + IRC + embedded HTTP server + command parser)
- `application/src/managers/PiShockWebSocketManager.cpp` — 1043
- `common/Config.cpp` — **953 lines** (manual field-by-field load/save for ~255 settings across one monolithic struct)
- `driver/IPC/IPCServer.cpp` — 919 (message dispatch god class)
- `application/src/managers/ButtplugManager.cpp` — 878
- `common/OSCManager.cpp` — 645 (handler dispatch god function, thread-unsafe callbacks)

Intended outcome of this sprint: eliminate the concrete runtime hazards (data races, thread leaks), carve UIManager into maintainable panel files, and establish a shared `IShockDeviceManager` interface so future integrations don't duplicate rate-limit/cooldown plumbing a sixth time. Twitch decomposition and Config domain split are scoped in but time-boxed; the rest becomes a prioritized backlog.

---

## Prioritized Findings

### 🔴 CRITICAL — Runtime hazards (must ship this sprint)

**C1. Unsynchronized OAuth token access across threads**
- `application/src/managers/TwitchManager.cpp:104,1059-1155,1390-1393`
- `access_token_` / `refresh_token_` written in `Update()` and read inside `ChatWorker()` thread without a mutex. IRC reconnect + token refresh can race, producing torn reads or stale auth.
- **Fix:** protect with a `std::mutex`; snapshot at the top of `ChatWorker` iterations.

**C2. Dangling `config_` pointer risk in worker threads**
- All 5 managers (`TwitchManager`, `PiShockManager`, `OpenShockManager`, `PiShockWebSocketManager`, `ButtplugManager`) hold raw `Config*` and dereference it from long-running threads. `ChatWorker` reads `config_->twitch_lock_command` etc. without guards (`TwitchManager.cpp:1390-1393`).
- **Fix:** define manager lifetime ⊂ config lifetime as an invariant enforced in `UIManager::Shutdown()`; add a `std::shared_mutex` around config reads that worker threads can share-lock; document the contract in `common/Config.hpp`.

**C3. Detached-thread leaks in shock managers**
- `OpenShockManager.cpp:549`, `PiShockManager.cpp:319` (`ExecuteActionAsync()` uses `std::thread(...).detach()`).
- Under rapid disobedience triggers, unbounded detached threads fire HTTP requests. If the manager is shut down mid-flight, detached threads can outlive the manager and touch destroyed state.
- **Fix:** replace with a single bounded worker + queue. Ensure `Shutdown()` drains in-flight work.

**C4. OSCManager singleton — unprotected callback assignment**
- `common/OSCManager.cpp:55,118,207-418` — the receive thread dispatches via member-function pointers set from `UIManager` without a mutex. If `UIManager` re-registers callbacks during runtime, the receive thread can read a half-written `std::function`.
- **Fix:** guard callback table with a mutex; OR freeze callbacks after `Initialize()` and forbid re-registration.

### 🟠 HIGH — Structural debt blocking maintenance

**H1. `UIManager` is a 6025-line god class**
- 11+ `Render*Tab()` methods, 45+ private methods, 30+ member variables, owns 5 external managers, manages timers, does device threshold checks (`CheckDevicePositionDeviations()` ≈ 280 lines, `RenderDeviceList()` ≈ 468 lines), writes config inline from render callbacks, uses `static char[]` ImGui buffers that persist across frames (`UIManager.cpp:3631,3691,3710,4368`).
- **Extraction seams identified:**
  1. Per-tab `*Panel` classes (`PiShockPanel`, `OpenShockPanel`, `ButtplugPanel`, `TwitchPanel`, `DevicesPanel`, `MainPanel`) — each implementing a small `ITabPanel { Render(); OnSave(); }` interface.
  2. `DeviceViewModel` owning `device_positions_`, role mapping, lock state — `RenderDeviceList` becomes thin.
  3. `TimerOrchestrator` for countdown / Twitch unlock / global-OOB / bite timers (currently interleaved in `Update()`).
  4. `ImGuiHelpers::TooltipLabel()` / `SafetyAgreementBlock()` / `LabeledInput<T>()` utilities — safety-warning block is copy-pasted 3x; tooltip pattern ≥50x.
- **Sprint scope:** cut UIManager down by extracting (a) `ImGuiHelpers` utility file, (b) `PiShockPanel`, (c) `OpenShockPanel`, (d) `ButtplugPanel`. Remaining tabs go to backlog.

**H2. No `IShockDeviceManager` interface — duplicated rate-limit/cooldown/error plumbing across 5 managers**
- Rate limiting: `PiShockManager.cpp:215-230`, `OpenShockManager.cpp:447-462`, `PiShockWebSocketManager.cpp:294-299`, `ButtplugManager.cpp:134-135`
- Cooldown: `PiShockManager.cpp:232-251`, `OpenShockManager.cpp:464-483`, `PiShockWebSocketManager.cpp:118-119`
- `ValidateConfiguration()` and `SetError()` duplicated in all 5 verbatim.
- **Fix:** introduce `common/IShockDeviceManager.hpp` with the lifecycle contract + a `ShockDeviceBase` abstract base that implements the shared `CanTriggerAction()` / `RecordCooldown()` / `SetError()` logic. Migrate `PiShockManager` and `OpenShockManager` first (smallest, most similar).

**H3. TwitchManager decomposition**
- `TwitchManager.cpp:681-909` = embedded WinSock OAuth HTTP server (~230 lines).
- `TwitchManager.cpp:1057-1316` = 260-line IRC `ChatWorker` monolith (socket + auth + recv loop).
- `TwitchManager.cpp:1318-1439` = `ProcessIRCMessage` command router.
- **Sprint scope:** extract two classes first — `TwitchOAuthCallbackServer` (the WinSock HTTP server) and `TwitchIrcClient` (ChatWorker + ProcessIRCMessage). `TwitchManager` becomes a thin coordinator. EventSub placeholder and command-handler split go to backlog. Thread-safety fixes (C1) happen *inside* this extraction — don't fix twice.

**H4. Config domain split + table-driven load/save**
- `common/Config.hpp:23-255` holds ~255 fields on one `Config` struct; `Config.cpp:136-576` loads them, `578-903` saves them, with legacy migration branches scattered through the load path (`Config.cpp:204-215,224-226,235-237`).
- **Sprint scope:** introduce domain sub-structs (`AudioConfig`, `OSCConfig`, `PiShockConfig`, `OpenShockConfig`, `ButtplugConfig`, `TwitchConfig`, `DeviceConfig`, `GeneralConfig`) as members of `Config`. Introduce one table-driven helper (`SerdeField<T>` with name + pointer-to-member) and convert **one** sub-struct (`AudioConfig`, the smallest) end-to-end as a proof. Rest of the conversion is backlog.
- **Backward compatibility:** existing JSON is flat; load path must accept both flat and nested on read, write nested on save. Test with a real pre-1.3 config file.

### 🟡 MEDIUM — Real debt, deferred to next sprint

**M1.** `IPCServer.cpp` (919 lines) — adding a message type touches 5 files. Replace with a message registry keyed by `MessageType`.
**M2.** `OSCManager::ProcessOSCMessage` (`OSCManager.cpp:259-418`) — 160-line if/else chain, string parsing repeated 6×. Extract a path→handler registry.
**M3.** `DeviceManager` is not the source of truth for device state (scattered across `DeviceManager`, `Config`, `OSCManager`, `UIManager`). Consolidate into an `IDeviceRepository`.
**M4.** Finish UIManager extraction (Twitch, Main, Devices tabs; `TimerOrchestrator`; `DeviceViewModel`).
**M5.** Finish Config migration (remaining sub-structs, remove flat-compat read path one release later).
**M6.** Finish `IShockDeviceManager` migration (Buttplug, PiShockWebSocket).

### 🟢 LOW — Nice-to-have

- `VRDriver.cpp:92-157` — quaternion↔matrix math embedded in `RunFrame`. Extract to `common/VRMath.hpp`.
- `main.cpp:20,98-109` — global `g_running`, no DI. Introduce an `AppContext`.
- Exception swallowing in main loop (`main.cpp:119-126`) should at least log the exception type.
- Unused WebSocket placeholder methods in `TwitchManager.cpp:1698-1716`.

---

## Sprint 1 Scope (ruthless cut, ~1-2 weeks)

**Working rule:** every step ends with a build + a manual UI verification. Do not stack unverified steps.

### Step 1 — C1: OAuth token mutex in TwitchManager (~0.5d)
Protect `access_token_` / `refresh_token_` / `token_expiry_` with a `std::mutex`. Provide `GetAccessTokenCopy()`, `GetRefreshTokenCopy()`, `SetTokens(...)`, `ClearTokens()` helpers. Snapshot the access token at the top of `ChatWorker` into a local. Make `IsTokenValid()` take the lock. Route every other call site through the helpers.

**Build:** `cmake --build build --config Release`

**Test:**
1. Launch SteamVR + StayPutVR. Go to Twitch tab, complete OAuth. Confirm "Connected" status appears and the log shows `✅ IRC authentication successful!`.
2. Send 10 `!lock` / `!unlock` commands from chat in quick succession — all must register.
3. Wait for token refresh (or force it) and repeat step 2.
4. Sanity check other tabs (PiShock, OpenShock, OSC) to confirm no crash.

### Step 2 — C2: Config lifetime contract (~0.5d)
Add a `mutable std::shared_mutex` to `Config` for cross-thread reads from managers. Document the manager-lifetime ⊂ config-lifetime invariant in `Config.hpp`. Add `std::shared_lock` in the hot paths in `TwitchManager::ChatWorker` (around `config_->twitch_lock_command` etc.) and the shock managers' action triggers. Writes from the UI thread use `std::unique_lock`.

**Build:** full rebuild.

**Test:**
1. Launch app, change settings on multiple tabs, confirm nothing hangs or crashes.
2. Trigger disobedience actions while editing config fields in the UI — no crash, no stale reads.
3. Clean shutdown from the window close button; confirm log shows orderly manager shutdown *before* config destruction.

### Step 3 — C3: Bounded async worker in shock managers (~0.5d)
Introduce `common/AsyncWorkQueue.hpp` (single worker thread + bounded queue, e.g., max 32 pending items). Replace `std::thread(...).detach()` in `OpenShockManager::ExecuteActionAsync` and `PiShockManager::ExecuteActionAsync`. `Shutdown()` must drain the queue before returning (stop accepting new work, join worker).

**Build:** full rebuild.

**Test:**
1. With PiShock enabled (or mock URL), trigger 20 rapid disobedience events — all fire, no crash.
2. Same with OpenShock.
3. Close the app mid-burst; confirm no log messages after the shutdown banner and no hung process.

### Step 4 — C4: OSCManager callback table mutex (~0.5d)
Guard the callback table with a mutex (or freeze-after-init rule — document whichever you pick). Document it in `OSCManager.hpp`.

**Build:** full rebuild.

**Test:**
1. Enable OSC, connect to VRChat, confirm lock/unlock via avatar parameters still works.
2. Toggle OSC off and on from the UI several times; no crash.
3. Trigger emergency stop via OSC stretch param; confirm it fires.

### Step 5 — H2a: `IShockDeviceManager` + `ShockDeviceBase`, migrate PiShock (~1d)
Add `common/IShockDeviceManager.hpp` and `common/ShockDeviceBase.{hpp,cpp}` with shared rate-limit / cooldown / `SetError` / `ValidateConfiguration` skeleton. Migrate `PiShockManager` first. Don't touch OpenShock yet.

**Build:** full rebuild.

**Test:**
1. PiShock tab: warning + disobedience fire with correct intensity + duration.
2. Cooldown timer still enforced (trigger twice rapidly; second should be suppressed).
3. Invalid credentials show the same error message as before.

### Step 6 — H2b: Migrate OpenShockManager onto `ShockDeviceBase` (~0.5d)

**Build:** full rebuild.

**Test:** same 3-step check as Step 5 on the OpenShock tab, including multi-device shock paths.

### Step 7 — H1a: Extract `ImGuiHelpers` utilities (~0.5d)
Mechanical extraction: `TooltipLabel()`, `SafetyAgreementBlock()`, `LabeledInput<T>()` from `UIManager.cpp`. Update existing call sites in UIManager without moving tabs yet.

**Build:** full rebuild.

**Test:**
1. Visit every tab. Tooltips render identically (hover state, wrapping).
2. Safety agreement blocks on PiShock / OpenShock / Buttplug tabs all behave the same (controls enable only after checkbox).

### Step 8 — H1b: Extract `PiShockPanel` into its own files (~0.5d)
Move `RenderPiShockTab` body + its owned state into `application/src/ui/panels/PiShockPanel.{hpp,cpp}`. Minimal `ITabPanel { Render(); }` interface.

**Build:** full rebuild.

**Test:**
1. PiShock tab renders identically.
2. All fields persist across save/reload.
3. Warning + disobedience still fire.

### Step 9 — H1c: Extract `OpenShockPanel` (~0.5d)
Same pattern as Step 8, for OpenShock tab.
**Build + Test:** mirror Step 8 on the OpenShock tab, including multi-device rows.

### Step 10 — H1d: Extract `ButtplugPanel` (~0.5d)
Same pattern for Buttplug (BPIO) tab.
**Build + Test:** confirm BPIO discovery, device list, vibration still works end-to-end.

### Step 11 — H3a: Extract `TwitchOAuthCallbackServer` from TwitchManager (~1d)
Move the WinSock HTTP server (`TwitchManager.cpp:681-909`) into `application/src/managers/twitch/TwitchOAuthCallbackServer.{hpp,cpp}`.

**Build:** full rebuild.

**Test:**
1. Fresh OAuth flow: disconnect, re-authorize, confirm browser redirect lands and token arrives.
2. Retry after closing the browser mid-flow; confirm the server recovers or reports a clear error.

### Step 12 — H3b: Extract `TwitchIrcClient` from TwitchManager (~1d)
Move `ChatWorker` + `ProcessIRCMessage` into `TwitchIrcClient.{hpp,cpp}`. TwitchManager becomes the coordinator. C1 fix already lives here — do not regress the token mutex when extracting.

**Build:** full rebuild.

**Test:**
1. Chat connects, `!lock` / `!unlock` fire as before.
2. Disconnect router briefly to force reconnect; confirm client recovers.
3. Run 100 rapid commands in a loop; no torn reads, no crash.

### Step 13 — H4: Config domain sub-structs + `AudioConfig` migration (~1.5d)
Add `AudioConfig`, `OSCConfig`, `PiShockConfig`, `OpenShockConfig`, `ButtplugConfig`, `TwitchConfig`, `DeviceConfig`, `GeneralConfig` as members of `Config`. Build `common/ConfigSerde.hpp` (table-driven `SerdeField<T>`). Migrate `AudioConfig` only. Flat-JSON read path must still work for pre-1.3 configs.

**Build:** full rebuild.

**Test:**
1. Backup a real pre-1.3 config file. Launch app → save → reload → all audio settings identical, all other settings identical.
2. Delete the config file. Launch app; defaults appear. Save → reload round-trip.
3. Confirm no crash on a malformed config (delete a required key by hand, relaunch).

### Sprint Exit Test — golden-path smoke
Once all steps above have shipped, run the README quickstart end-to-end one more time:
1. SteamVR + StayPutVR running, "Connected to Driver" visible.
2. Identify and assign device roles on the Devices tab.
3. Enable OSC, connect to VRChat avatar with StayPutVR cuffs.
4. Enable PiShock (or mock), configure intensities.
5. Lock cuffs in-game, move out of safe zone, confirm warning fires; move into non-compliance zone, confirm disobedience action fires.
6. Trigger emergency stop via OSC stretch param; confirm everything unlocks and blocks further output.

**Total:** ~9d of work + verification pauses. If the sprint runs hot, cut in this order: Step 10 (Buttplug panel), Step 12 (TwitchIrcClient), Step 13 (Config migration).

---

## Critical Files to Touch

- `application/src/ui/UIManager.{cpp,hpp}` — god class, extracting 3 tabs + helpers
- `application/src/managers/TwitchManager.{cpp,hpp}` — C1 fix + extract OAuth server and IRC client
- `application/src/managers/PiShockManager.{cpp,hpp}` — C3 fix + migrate to `ShockDeviceBase`
- `application/src/managers/OpenShockManager.{cpp,hpp}` — C3 fix + migrate to `ShockDeviceBase`
- `common/OSCManager.{cpp,hpp}` — C4 callback table mutex
- `common/Config.{cpp,hpp}` — shared_mutex, domain sub-structs, table-driven serde, flat-compat
- New: `common/IShockDeviceManager.hpp`, `common/ShockDeviceBase.{hpp,cpp}`, `common/AsyncWorkQueue.hpp`, `common/ConfigSerde.hpp`
- New: `application/src/ui/ImGuiHelpers.{hpp,cpp}`, `application/src/ui/panels/PiShockPanel.{hpp,cpp}`, `OpenShockPanel.{hpp,cpp}`, `ButtplugPanel.{hpp,cpp}`
- New: `application/src/managers/twitch/TwitchOAuthCallbackServer.{hpp,cpp}`, `TwitchIrcClient.{hpp,cpp}`

## Reuse / Don't Reinvent

- `common/HttpClient.hpp:58-112` already has `SendPiShockCommand*` and `SendOpenShockCommand*` helpers — `ShockDeviceBase` should delegate to these, not wrap them again.
- `common/WebSocketClient` already provides a receive thread + callback pattern — `PiShockWebSocketManager` and `ButtplugManager` use it correctly; `TwitchIrcClient` should not reimplement WinSock loops where an existing abstraction suffices.
- `Logger` is global and thread-safe — keep using it; don't introduce per-manager loggers.

## Out of Scope (explicit)

- IPCServer refactor (M1)
- OSCManager dispatch registry (M2)
- DeviceManager repository consolidation (M3)
- Twitch EventSub, command-handler split
- Remaining UIManager tabs (Twitch, Main, Devices), TimerOrchestrator, DeviceViewModel
- Config migration of non-Audio sub-structs
- `AppContext`/DI, quaternion math extraction, low-priority items
- Linux build port

---

## Progress Log

| Step | Status | Notes |
|------|--------|-------|
| 1 — C1 OAuth token mutex | **🟡 implemented, awaiting Windows build + manual test** | See commit on `tech-debt/sprint-1`. Diff summary below. |
| 2 — C2 Config lifetime shared_mutex | ⬜ pending | |
| 3 — C3 Bounded async worker | ⬜ pending | |
| 4 — C4 OSCManager callback mutex | ⬜ pending | |
| 5 — H2a IShockDeviceManager + PiShock | ⬜ pending | |
| 6 — H2b OpenShock migration | ⬜ pending | |
| 7 — H1a ImGuiHelpers extraction | ⬜ pending | |
| 8 — H1b PiShockPanel extraction | ⬜ pending | |
| 9 — H1c OpenShockPanel extraction | ⬜ pending | |
| 10 — H1d ButtplugPanel extraction | ⬜ pending | |
| 11 — H3a TwitchOAuthCallbackServer | ⬜ pending | |
| 12 — H3b TwitchIrcClient | ⬜ pending | |
| 13 — H4 Config domain sub-structs + AudioConfig | ⬜ pending | |

### Step 1 — diff summary

**`application/src/managers/TwitchManager.hpp`**
- Added `mutable std::mutex token_mutex_` protecting `access_token_`, `refresh_token_`, `token_expiry_`.
- Added four private helpers: `GetAccessTokenCopy()`, `GetRefreshTokenCopy()`, `SetTokens(...)`, `ClearTokens()`.

**`application/src/managers/TwitchManager.cpp`**
- Constructor: explicit `token_expiry_{}` initializer (was implicitly default-constructed before).
- `IsTokenValid()` now takes the lock.
- Every call site that used to touch `access_token_` / `refresh_token_` directly now goes through a helper:
  - `ConnectToTwitch`, `DisconnectFromTwitch`, `HandleOAuthCallback`, `RefreshAccessToken`, `ValidateAccessToken`, `ValidateTokenScopes`, `MakeAPIRequest`, `BuildAuthHeader`.
- **`ChatWorker`** (the worker-thread path that motivated this fix): snapshots the token once under the lock into a `const std::string access_token` local at function entry and uses the local everywhere (IRC `PASS` message, logging, length checks).
- A grep confirmed the only remaining raw `access_token_` / `refresh_token_` references are the 6 lines inside the helpers themselves + `IsTokenValid`.

**Verify on Windows:**
```
cmake --build build --config Release
```
Then run the Step 1 test list above. If green, mark the row `✅ verified` and move to Step 2.
