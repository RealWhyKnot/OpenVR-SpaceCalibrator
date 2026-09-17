# Space Calibrator with tracker smoothing

Fork of Space Calibrator 1.5.1. I added a one euro filter on the calibrated trackers to smooth them out.

## Install

Close SteamVR, unzip somewhere permanent, run `install.ps1`. It registers this driver, unregisters any other `01spacecalibrator` or `000spacecalibrator` copy (Steam build included), turns on `activateMultipleDrivers`, and sets the overlay to autolaunch. `uninstall.ps1` undoes it.

Two things it won't fix: a driver folder inside `<SteamVR>\drivers\` loads regardless of registration, so remove or rename that one yourself (the installer warns you, and the overlay's Settings page has a removal panel). And don't launch the Steam copy while this one is registered: both drivers rewrite the same poses.

The overlay checks GitHub for updates on start and asks before touching anything. The toggle and a "Check now" button are on the Settings tab.

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
