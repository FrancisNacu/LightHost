Light Host
---

A simple VST/AU host for OS X, Windows, and Linux that sits in the menu/task bar.

### Features

See [#1](https://github.com/rolandoislas/LightHost/issues/1)

### Settings backup

Right-click the Light Host tray icon and choose **Export Settings...** to save the current settings to a `.settings` file. Choose **Import Settings...** to replace the current settings from a previously exported file. Light Host closes after a successful import; reopen it to apply the imported audio and plugin settings.

### Windows build

The **Build Windows executable** GitHub Actions workflow builds a 64-bit Windows executable on a hosted Windows runner when changes are pushed to `master`, or when started manually from the repository's **Actions** tab. Download the `LightHost-Windows-x64` artifact from a completed run. Artifacts are kept for 7 days.

The workflow builds against JUCE 4.2.4 and Steinberg VST3 SDK 3.6.12, retaining VST3 hosting. ASIO is disabled because its separate SDK is not included. The artifact includes the executable and applicable license files.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)