# Installation

Windows 10 or 11 x64. Close the DAW and Standalone first. Verify the installer against the SHA256SUMS.txt file from the same release before running it. The candidate is unsigned. On a clean system that reports missing MSVC libraries, install the official [Microsoft Visual C++ Redistributable x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

1. Download the setup EXE from [GitHub Releases](https://github.com/yavmaxim-cmd/skratch-exe-beta-site/releases) and run it with administrator permissions. If you already have a portable ZIP, extract it for manual installation.
2. Select Instrument VST3, FX VST3, Standalone and/or the synthetic WAV.
3. The installer puts VST3 bundles in `C:\Program Files\Common Files\VST3`. For manual installation, copy the complete selected .vst3 bundle folders into that directory. Standalone can run from the ZIP or from the Start menu after setup.
4. Existing manually installed copies with the same bundle names must be moved to a backup folder outside VST3 search paths before installing. Keep the old copies and a separate copy of your DAW project. Launch the DAW and rescan.
5. Use Windows Settings → Apps → Installed apps to uninstall a setup-managed copy. Windows user settings and presets outside the shipped program files remain on disk. Removing a component from the selection during an update leaves an already installed component in place. Uninstall and reinstall with the desired selection to remove it.

A new .vst3 build retains the same class ID as its development predecessor. FL Studio and other hosts may cache old version metadata. Rescan with verification after a change. See [FL Studio plug-in installation](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm).

## Beta limitations

- Install, uninstall and update on clean Windows, in-host FL Studio behavior and physical USB/Bluetooth controllers still need verification. The repository includes a clean-VM installer test.
- Standalone uses the Windows audio backends included in this build. VST3 uses the audio device configured in your DAW.
- Scratch recordings capture performance controls. Render the audio in your DAW to get an audio file.
- FX File Deck may reference external samples. Keep those files available when reopening or moving a project.
