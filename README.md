# Space Calibrator with tracker smoothing

Fork of the 1.5.1 line of Space Calibrator. This fork adds a one euro filter on the calibrated trackers to smooth them out.

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
