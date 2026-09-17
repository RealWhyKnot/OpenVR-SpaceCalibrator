# Space Calibrator with tracker smoothing

Fork of Space Calibrator 1.5.1. I added a one euro filter on the calibrated trackers to smooth them out.

## Install

Grab `OpenVR-SpaceCalibrator-Setup-<version>.exe` from Releases and run it. No admin prompt: it installs per-user, registers the driver, unregisters any other `01spacecalibrator` or `000spacecalibrator` copy (Steam build included), turns on `activateMultipleDrivers`, and sets the overlay to autolaunch. If SteamVR isn't running during setup, the app finishes its SteamVR registration the next time SteamVR starts.

Uninstall from Windows Apps & features. That removes the driver registration, the app, and all of its settings and logs, calibration included. It leaves `activateMultipleDrivers` on and doesn't re-enable the Steam copy's autolaunch.

Prefer a portable copy? The zip is still on every release: close SteamVR, unzip somewhere permanent, run `install.ps1`. `uninstall.ps1` unregisters it but leaves the files and settings behind.

The overlay checks GitHub for updates on start and installs them after SteamVR closes. The toggles and a "Check now" button are on the Settings tab.

## Build

Visual Studio 2022 with the C++ workload, CMake 3.24+, git.

```
git clone --recurse-submodules https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator.git
cd OpenVR-SpaceCalibrator
./build.ps1
```

The driver lands in `build/01spacecalibrator`, the overlay in `build/artifacts/Release`, and `scripts/install.ps1` with no arguments registers those. `lint.ps1` fixes formatting, `-Check` only checks.

## Releases

A `vYYYY.M.D.N` tag publishes a release, `-beta` suffix a prerelease; notes come from the commit subjects. A nightly job tags a beta whenever main has moved.

## License

MIT. See LICENSE and NOTICE.
