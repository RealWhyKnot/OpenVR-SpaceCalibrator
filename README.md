# Space Calibrator with tracker smoothing

Fork of the 1.5.1 line of Space Calibrator, the SteamVR tool that lines up trackers from one
tracking system with another and keeps them lined up while you play. This fork adds a one euro
filter on the calibrated trackers. It runs inside the driver that already applies the calibration,
so nothing extra shows up in SteamVR and games see the trackers they always saw. They just stop
jittering when you hold still.

## Smoothing

The driver already rewrites every target-space pose on its way into SteamVR, since that is how the
calibration gets applied at all. Smoothing hooks the same spot. Position runs through a one euro
filter first, in the tracker's own space, and then the calibration transform happens as before.

Rotation isn't filtered. An earlier version did filter it, and that turned out to be the thing
making a twitchy tracker feel worse, so it's gone. Added lag would show up there first anyway.

Prediction is the other half. SteamVR extrapolates each pose forward to photon time, and on a noisy
tracker that extrapolation is what you actually see as overshoot, so the driver walks its prediction
back by however much smoothing you asked for. Continuous calibration corrections never enter the
filter; they land exactly as they did before.

Controls live in the overlay under "Tracker smoothing":

- Smoothing, 0 to 100%, 0 being off. Higher is calmer at rest. The filter opens back up the moment
  you move, so real motion trails by under 15 mm even at 100%.
- Also smooth controllers, off by default. Latency is much easier to feel in your hands.
- A live table of raw against smoothed frame-to-frame movement per device, and a count of filter
  restarts, which happen after a dropout or a jump.

Only devices in the calibrated target space are smoothed, so you need an active profile.

## Install a release

Close SteamVR, unzip somewhere you won't move it, and run `install.ps1` from the unzipped folder. It
registers the driver folder, unregisters any other copy of the `01spacecalibrator` driver it finds
(the Steam build included), turns on `activateMultipleDrivers`, and registers the overlay so SteamVR
autolaunches it. Start SteamVR and it's in the dashboard where it always was.

Don't launch the Steam copy while this one is registered. Both drivers would rewrite the same poses.
`uninstall.ps1` reverses the registration.

### Updates

An installed copy checks GitHub when the overlay starts and asks before it does anything. Beta
builds see betas, stable builds see only stable releases, dev builds don't check. Accept and it
pulls the zip, verifies the SHA-256, and swaps the files once SteamVR has closed. Skip and it stops
asking about that version. The Settings tab has the startup toggle and a "Check now" button.

## Build

Visual Studio 2022 or newer with the C++ workload, CMake 3.24 or newer, git.

```
git clone --recurse-submodules https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator.git
cd OpenVR-SpaceCalibrator
./build.ps1
```

`build.ps1` stamps `version.txt`, enables the repo git hooks, configures into `build/`, builds
Release, and runs the tests. `-Channel beta` or `-Channel release` sets the channel baked into the
version line. You end up with `build/01spacecalibrator` for the driver and
`build/artifacts/Release` for the overlay, and `scripts/install.ps1` with no arguments registers
those build outputs where they sit.

`lint.ps1 -Check` runs clang-format and clang-tidy over `src/` and `tests/`. Without `-Check` it
applies the fixes.

## Releases

A `vYYYY.M.D.N` tag publishes a release, `vYYYY.M.D.N-beta` a prerelease. The workflow builds,
tests, zips the driver and the overlay together with the install scripts, attaches a SHA-256 file,
and writes the notes from the conventional commit subjects since the previous tag. A nightly job
tags a beta whenever `main` has moved since the last tag.

## License

MIT. See LICENSE and NOTICE.
