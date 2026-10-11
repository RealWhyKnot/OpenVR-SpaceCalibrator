# Space Calibrator

My fork of Space Calibrator 1.5.1. It lines up two tracking systems in SteamVR, for example lighthouse trackers with a standalone headset. I added tracker smoothing, finger smoothing for Index controllers, joystick acceleration and base station power control over Bluetooth LE.

## Install

Download `OpenVR-SpaceCalibrator-Setup-<version>.exe` from Releases and run it. It installs per user without an admin prompt, registers the driver, unregisters any other Space Calibrator driver (the Steam build too), turns on `activateMultipleDrivers` and starts the overlay with SteamVR.

Uninstall from Windows Apps & features. That removes the app, its driver registration and all of its settings, calibration included.

For a portable copy, unzip the release zip somewhere permanent and run `install.ps1` with SteamVR closed.

## Updates

The overlay checks GitHub when it starts and installs new versions after SteamVR closes. The Updates panel on the Settings page shows the installed version, and if an update fails it says why. Details are in `%LOCALAPPDATA%\SpaceCalibrator\update.log`.

## Reporting problems

Tick "Enable debug logs" on the Calibration page and reproduce the problem. Send the logs from `%USERPROFILE%\AppData\LocalLow\SpaceCalibrator\Logs`, plus `update.log` for update problems.

## Build

Visual Studio 2022 with the C++ workload, CMake 3.24 or newer, and git.

```
git clone --recurse-submodules https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator.git
cd OpenVR-SpaceCalibrator
./build.ps1
```

`build.ps1` builds and runs the tests. `scripts/install.ps1` registers that build with SteamVR. `lint.ps1` fixes formatting.

Pushing a `vYYYY.M.D.N` tag publishes a release, and a `-beta` suffix makes it a prerelease.

## License

MIT. See LICENSE and NOTICE.
