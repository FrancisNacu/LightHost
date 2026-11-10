Light Host
---

A simple VST/AU host for OS X, Windows, and Linux that sits in the menu/task bar.

### Features

See [#1](https://github.com/rolandoislas/LightHost/issues/1)

### Backup and restore

Configuration and audio logs are stored in the `Data` folder beside the executable, regardless of the working directory or shortcut used to launch it. Existing settings and logs beside the executable move there automatically. AppData and previous executable locations are never searched for settings. Backup dialogs also default to the executable's directory. To preserve your setup when moving the app, move the executable, `Data`, and `VST Plugins` together into a writable directory. Moving only the executable uses the destination folder's configuration, or creates fresh settings there if none exist. Plugin paths inside the app folder and their saved states follow the new location automatically; externally installed plugins retain their existing paths.

Open Settings from the Light Host tray window and choose **Export backup...** to create a `.lightbak` file containing your Light Host settings, plugin list, active plugin states, audio device settings, and discovered file-based plugin binaries. Choose **Import backup...** to restore the settings and missing plugin files into the `VST Plugins` folder beside the executable. Light Host automatically reloads after a successful import to apply the restored settings. Import accepts only `.lightbak` files, which use ZIP compression internally.

Existing plugin files are never overwritten. Plugin installers, license data, external dependencies, and plugins managed by the operating system are not included; those may need to be installed or authorized separately. Backups can be large, and plugin binaries may be subject to their authors' license terms.

### Windows build

When **Run on startup** is enabled, launching Light Host refreshes its Windows startup entry to the current executable location. After moving or renaming the executable, launch it once from the new location before signing out. Turning startup off removes the entry; subsequent launches leave it off. If you keep multiple copies, the last copy launched becomes the startup target.

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

Audio diagnostics are saved as `Data/Light Host.audio.log` beside the executable. They summarize stream settings, device errors, callbacks that exceed the buffer duration, and buffers containing NaN/infinite output. Invalid samples are silenced. Diagnostic writes run outside the audio callback, are batched every five seconds, and the log is trimmed to approximately 128 KiB. Diagnostics do not record microphone audio. They cannot detect every driver underrun or distortion consisting of otherwise finite samples.

The regression checks also cover channel migration, invalid-output containment, processing overruns, and propagation of rate/buffer changes when a device restarts. Physical ASIO recovery and third-party plugin behavior still require testing with the actual device and plugins.

Plugin state, order, bypass, and display names use the plugin format, ID, and full location as their identity; existing settings are migrated automatically. Version 2 backups include both available and active plugins, preserve single-file versus bundle layouts, and remap identity-based settings when restoring to another location. Original version 1 backups without a layout flag remain supported. Older app versions cannot restore version 2 backups.

Only effects with at least two input and two output channels are supported in the stereo chain. Other layouts are skipped with a warning, and the remaining chain continues passing audio. Plugin state snapshots synchronize with processing in graph-then-processor lock order; a plugin with a slow state callback can briefly interrupt audio during saving.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)
