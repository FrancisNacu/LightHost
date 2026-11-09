Light Host
---

A simple VST/AU host for OS X, Windows, and Linux that sits in the menu/task bar.

### Features

See [#1](https://github.com/rolandoislas/LightHost/issues/1)

### Backup and restore

Left-click the Light Host tray icon and choose **Export Backup...** to create a ZIP archive containing your Light Host settings, plugin list, active plugin states, audio device settings, and discovered file-based plugin binaries. Choose **Import Backup...** to install plugin files into the first scan location configured for each format on this computer and restore the settings. Light Host closes after a successful import; reopen it to apply the restored settings.

Existing plugin files are never overwritten. Plugin installers, license data, external dependencies, and plugins managed by the operating system are not included; those may need to be installed or authorized separately. Backups can be large, and plugin binaries may be subject to their authors' license terms.

### Windows build

The **Build Windows executable** GitHub Actions workflow builds a 64-bit Windows executable on a hosted Windows runner when changes are pushed to `master`, or when started manually from the repository's **Actions** tab. Download the `LightHost-Windows-x64` artifact from a completed run. Artifacts are kept for 7 days.

The workflow builds against JUCE 4.2.4 and Steinberg VST3 SDK 3.6.12, retaining VST3 hosting. ASIO is disabled because its separate SDK is not included. The artifact includes the executable and applicable license files.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)