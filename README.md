# SKRATCH.EXE 0.9.0-beta

Scratch workstation for Windows x64. Instrument VST3 is a generator. FX VST3 processes live input or plays its file deck. Standalone includes the FX engine and its own audio-device settings.

- [Installation and beta limitations](INSTALLATION.md) covers setup, updates and compatibility. [Quick Start](QUICK_START.md) covers FL Studio and Standalone.
- [Controls](CONTROLS.md) covers scratching, the mixer, gamepad, patterns and recording.
- [MIDI and Piano Roll](MIDI_PIANO_ROLL.md) covers note mapping, MIDI Learn and timing.

The Windows x64 installer is available from [GitHub Releases](https://github.com/yavmaxim-cmd/skratch-exe-beta-site/releases). The plug-in keeps its existing VST3 class IDs and preset schema.

## Build from source

Use Visual Studio Build Tools with the C++ workload, Windows SDK, CMake 3.22 or later and the accompanying JUCE 8.0.13 source. Extract `Dependencies/JUCE-8.0.13.zip` and point CMake at the directory that contains JUCE's CMakeLists.txt.

```powershell
cmake -S . -B build -A x64 -DJUCE_SOURCE_DIR=C:/path/to/JUCE-8.0.13
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For the installer, stage the Release bundles under `Windows`, the generated WAV under `Demo`, notices under `Licenses`, Markdown instructions under `Docs`, and the diagnostic script under `Tools`. See [Installer build](Installer/README.md) for the staging command. See [License Notices](LICENSE_NOTICES.md) for dependency and distribution terms.
