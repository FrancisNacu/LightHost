Light Host
---

A simple VST/AU host for OS X, Windows, and Linux that sits in the menu/task bar.

### Features

See [#1](https://github.com/rolandoislas/LightHost/issues/1)

### Backup and restore

Left-click the Light Host tray icon and choose **Export Backup...** to create a ZIP archive containing your Light Host settings, plugin list, active plugin states, audio device settings, and discovered file-based plugin binaries. Choose **Import Backup...** to install plugin files into the first scan location configured for each format on this computer and restore the settings. Light Host closes after a successful import; reopen it to apply the restored settings.

Existing plugin files are never overwritten. Plugin installers, license data, external dependencies, and plugins managed by the operating system are not included; those may need to be installed or authorized separately. Backups can be large, and plugin binaries may be subject to their authors' license terms.

### Windows build

To build the 64-bit Windows executable locally:

1. Install Git for Windows and Visual Studio 2022 with the **Desktop development with C++** workload and **C++ CMake tools** component.
2. From the repository root, run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
   ```

The script downloads the pinned JUCE and Steinberg VST3 SDK revisions into `lib/` if they are not already present, builds Projucer, generates the Visual Studio project, and builds the application in Release mode. The executable is written to `Builds/Windows/Release/Light Host.exe`. To build Debug instead, add `-Configuration Debug` to the command.

The build retains VST3 hosting. ASIO is enabled when the Steinberg ASIO SDK is found at `lib/ASIOSDK`, `C:\Dev\SDKs\ASIOSDK`, or the location specified by `ASIO_SDK_PATH`. You can also pass `-AsioSdkPath C:\path\to\ASIOSDK`. If the SDK is unavailable, the script builds without ASIO and prints a warning. Obtain the SDK from Steinberg and follow its license terms. Generated build output, JUCE project files, and downloaded dependencies are ignored by Git.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)