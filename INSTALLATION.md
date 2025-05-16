# StayPutVR Installation and Testing Guide

This document explains how to build, install, and test the StayPutVR driver for SteamVR.

## Building the Project

### Prerequisites

- CMake 3.15 or higher
- Visual Studio 2019 or higher (with C++ development tools)
- OpenVR SDK 2.5.1 or compatible
- SteamVR installed

### Build Steps

1. **Clone the repository**:
   ```
   git clone https://github.com/YourUsername/StayPutVR.git
   cd StayPutVR
   ```

2. **Configure CMake**:
   
   Edit the CMakeLists.txt file if needed to set the correct paths:
   - `OPENVR_SDK_PATH`: Path to your OpenVR SDK
   - `STEAM_PATH`: Path to your Steam installation
   - `STEAMVR_PATH`: Path to your SteamVR installation

3. **Generate build files**:
   ```
   mkdir build
   cd build
   cmake ..
   ```

   Alternatively, use CMake GUI or Visual Studio's CMake integration.

4. **Build the project**:
   ```
   cmake --build . --config Debug
   ```
   
   For release build:
   ```
   cmake --build . --config Release
   ```

## Installation

There are two methods to install the driver:

### Method 1: Using CMake Install Target

This is the recommended method as it automates the process:

```
cmake --build . --target install
```

The driver files will be installed to:
`[STEAMVR_PATH]/drivers/stayputvr/`

### Method 2: Manual Installation

If you prefer to install manually:

1. Create the driver directory structure:
   ```
   mkdir -p "[STEAMVR_PATH]/drivers/stayputvr/bin/win64"
   ```

2. Copy the driver DLL file:
   ```
   copy driver_stayputvr.dll "[STEAMVR_PATH]/drivers/stayputvr/bin/win64/"
   ```

3. Copy the driver manifest:
   ```
   copy "../../driver.vrdrivermanifest" "[STEAMVR_PATH]/drivers/stayputvr/"
   ```

## Registering the Driver with SteamVR

You must register the driver with SteamVR for it to be discovered:

```
cd "[STEAM_PATH]/bin"
vrpathreg.exe adddriver "[STEAMVR_PATH]/drivers/stayputvr"
```

To verify the driver is registered:
```
vrpathreg.exe show
```

You should see your driver path listed in the output.

## Testing the Driver

### Method 1: Launch with SteamVR

1. Close SteamVR if it's already running
2. Launch SteamVR from Steam
3. The StayPutVR UI should appear automatically when SteamVR starts

### Method 2: Using the CMake Install and Run Target

If you've configured the `install_and_run` target in CMake:

```
cmake --build . --target install_and_run
```

This will install the driver and register it with SteamVR in one step.

## Troubleshooting

### Driver Not Loading

1. Check if the driver is registered:
   ```
   cd "[STEAM_PATH]/bin"
   vrpathreg.exe show
   ```

2. Check SteamVR logs for errors:
   - Windows: `%PROGRAMDATA%\Steam\logs\vrserver.txt`
   - Look for lines containing "StayPutVR" or errors related to loading drivers

3. Verify the DLL dependencies:
   - Use a tool like Dependency Walker to check if all required DLLs are available

### UI Not Appearing

1. Check if the driver is initialized properly:
   - Look for "Activating StayPutVR driver..." in the logs
   - Look for any OpenGL/GLFW initialization errors

2. Check if the window is being created off-screen:
   - Try adjusting the window position in UIManager.cpp

## Uninstalling

To unregister the driver:
```
cd "[STEAM_PATH]/bin"
vrpathreg.exe removedriver "[STEAMVR_PATH]/drivers/stayputvr"
```

To completely remove:
1. Unregister using the command above
2. Delete the driver folder:
   ```
   rmdir /s /q "[STEAMVR_PATH]/drivers/stayputvr"
   ``` 