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

Add `-RunTests` to build and run the audio routing regression checks. These use in-memory stereo buffers without opening an audio device. They verify empty-chain passthrough, chained processing, bypass/resume, preservation of plugin instances, and missing-plugin handling. They also verify that bypassed plugins receive no processing calls. Test binaries are written to `Builds/Tests/Release/` (or `Debug/`).

### Audio stability

Light Host processes up to two selected input and output channels. Older settings that enabled more channels retain their first two selected channels; sample rate and buffer size are preserved. The Windows build applies the tracked JUCE ASIO patch before generating the project, so repeated driver resynchronization requests cannot keep postponing a pending reset.

Use **Settings → Audio → Restart audio** to reopen the device and recreate the plugin chain while retaining plugin settings. This briefly interrupts audio and closes plugin editors. **Reset device** in the device selector only reopens the device and reprepares the existing plugins.

Audio diagnostics are saved beside the settings file as `Light Host.audio.log` (on Windows, normally `%APPDATA%\Light Host\Light Host.audio.log`). They summarize stream settings, device errors, callbacks that exceed the buffer duration, and buffers containing NaN/infinite output. Invalid samples are silenced. Diagnostic writes run outside the audio callback, are batched every five seconds, and the log is trimmed to approximately 128 KiB. Diagnostics do not record microphone audio. They cannot detect every driver underrun or distortion consisting of otherwise finite samples.

The regression checks also cover channel migration, invalid-output containment, processing overruns, and propagation of rate/buffer changes when a device restarts. Physical ASIO recovery and third-party plugin behavior still require testing with the actual device and plugins.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)
