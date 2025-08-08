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
