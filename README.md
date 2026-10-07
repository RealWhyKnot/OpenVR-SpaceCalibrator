# Space Calibrator with tracker smoothing

Fork of Space Calibrator 1.5.1. I added a one euro filter that smooths the calibrated trackers.

## Install

Download `OpenVR-SpaceCalibrator-Setup-<version>.exe` from Releases and run it. It installs per-user, without an admin prompt. Setup registers the driver, unregisters any other `01spacecalibrator` or `000spacecalibrator` copy (the Steam build too), turns on `activateMultipleDrivers` and sets the overlay to autolaunch. If SteamVR isn't running during setup, the app finishes registering with SteamVR the next time it starts.

Uninstall it from Windows Apps & features. That removes the driver registration, the app and all of its settings and logs, calibration included. `activateMultipleDrivers` stays on, and the Steam copy's autolaunch isn't turned back on.

For a portable copy, close SteamVR, unzip the release zip somewhere permanent and run `install.ps1`. Its `uninstall.ps1` unregisters the driver but leaves the files and settings where they are.

Updates come from GitHub. The overlay checks when it starts and installs a new version after SteamVR closes, and the Settings tab has the toggles and a "Check now" button.

## Build

Visual Studio 2022 with the C++ workload, CMake 3.24 or newer, and git.

```
git clone --recurse-submodules https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator.git
cd OpenVR-SpaceCalibrator
./build.ps1
```

The driver is built to `build/01spacecalibrator` and the overlay to `build/artifacts/Release`, and running `scripts/install.ps1` with no arguments registers that build with SteamVR. `lint.ps1` fixes formatting. Add `-Check` to only report it.

## Releases

Pushing a `vYYYY.M.D.N` tag publishes a release, and a `-beta` suffix makes it a prerelease. The notes are built from the commit subjects. A nightly job tags a beta when main has new commits.

## License

MIT. See LICENSE and NOTICE.
