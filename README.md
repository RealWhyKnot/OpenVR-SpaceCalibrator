# Space Calibrator with tracker smoothing

A fork of the 1.5.1 line of Space Calibrator, the SteamVR tool that lines up devices from one
tracking system with another, with continuous calibration. This fork adds an optional one euro
filter that smooths the calibrated trackers in place. No virtual devices, no second driver: games
see the same trackers they always did, just steadier at rest.

## What smoothing does

The driver already rewrites every target-space pose on its way into SteamVR to apply the
calibration. With smoothing on, it first runs a one euro filter on the tracker's position and
rotation, in the tracker's own space, before the calibration transform is applied. Velocities are
recomputed from the filtered motion so SteamVR's prediction stays consistent. Continuous
calibration corrections never enter the filter, so they behave exactly as before.

Controls live in the overlay under "Tracker smoothing":

- Smooth trackers: master switch. Takes effect immediately and is saved with the profile.
- Also smooth controllers: off by default; controllers are latency sensitive.
- Jitter cutoff (Hz): lower is calmer at rest, but lags slow movement more.
- Responsiveness: higher opens the filter sooner on fast moves so quick motion lags less.
- A live table shows raw versus smoothed frame-to-frame movement per device, and how often the
  filter restarted after a gap or a jump.

Smoothing applies to devices in the calibrated target space, so a profile has to be active.

## Install from a release

1. Close SteamVR.
2. Unzip the release anywhere permanent.
3. Run `install.ps1` from the unzipped folder. It registers the driver folder with SteamVR,
   unregisters any other copy of the `01spacecalibrator` driver (including the Steam build), turns
   on `activateMultipleDrivers`, and registers the overlay so it autolaunches with SteamVR.
4. Start SteamVR. The overlay appears in the dashboard as usual.

Do not launch the Steam copy of Space Calibrator while this one is registered; two copies of the
driver would both rewrite poses. `uninstall.ps1` reverses the registration.

## Build from source

Requirements: Visual Studio 2022 or newer with the C++ workload, CMake 3.24 or newer, git.

```
git clone --recurse-submodules https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator.git
cd OpenVR-SpaceCalibrator
./build.ps1
```

`build.ps1` stamps `version.txt`, enables the repo git hooks, configures into `build/`, builds
Release, and runs the tests. `build.ps1 -Channel beta` or `-Channel release` sets the channel baked
into the version line. Outputs: `build/01spacecalibrator` (driver) and `build/artifacts/Release`
(overlay). `scripts/install.ps1` with no arguments registers those build outputs directly.

`lint.ps1 -Check` runs clang-format and clang-tidy over `src/` and `tests/`; `lint.ps1` without
`-Check` applies the fixes.

## Releases

Tags of the form `vYYYY.M.D.N` publish a release; `vYYYY.M.D.N-beta` publishes a prerelease. The
release workflow builds, tests, zips the driver and overlay with the install scripts, attaches a
SHA-256 file, and writes the notes from the conventional commit subjects since the previous tag.
A nightly job tags a beta whenever `main` moved since the last tag.

## License

MIT. See LICENSE and NOTICE.
