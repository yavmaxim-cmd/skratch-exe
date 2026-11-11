"""Stage a Windows x64 beta payload from an existing Release build."""
from argparse import ArgumentParser
import ctypes
import json
from pathlib import Path
import re
from shutil import copy2, copytree
from subprocess import run
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTS = (
    "README.md", "INSTALLATION.md", "QUICK_START.md",
    "CONTROLS.md", "MIDI_PIANO_ROLL.md", "LICENSE_NOTICES.md",
)
JUCE_NOTICES = {
    "FLAC.txt": "modules/juce_audio_formats/codecs/flac/Flac Licence.txt",
    "Ogg-Vorbis.txt": "modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt",
    "IJG-JPEG.txt": "modules/juce_graphics/image_formats/jpglib/README",
    "PNG.txt": "modules/juce_graphics/image_formats/pnglib/LICENSE",
    "zlib.txt": "modules/juce_core/zip/zlib/LICENSE",
    "HarfBuzz.txt": "modules/juce_graphics/fonts/harfbuzz/COPYING",
    "SheenBidi.txt": "modules/juce_graphics/unicode/sheenbidi/LICENSE",
}


def module_info(bundle):
    metadata = (bundle / "Contents/Resources/moduleinfo.json").read_text(encoding="utf-8")
    return json.loads(re.sub(r",\s*([}\]])", r"\1", metadata))


def check_windows_version(binary):
    version_api = ctypes.windll.version
    filename = str(binary)
    size = version_api.GetFileVersionInfoSizeW(filename, None)
    data = ctypes.create_string_buffer(size)
    info = ctypes.c_void_p()
    length = ctypes.c_uint()
    if (not size or not version_api.GetFileVersionInfoW(filename, 0, size, data) or
            not version_api.VerQueryValueW(data, "\\", ctypes.byref(info), ctypes.byref(length)) or
            length.value < 16):
        raise ValueError(f"Missing Windows file version: {binary}")
    signature, _, high, low = (ctypes.c_uint32 * 4).from_address(info.value)
    actual = (high >> 16, high & 0xffff, low >> 16, low & 0xffff)
    if signature != 0xFEEF04BD or actual != (0, 9, 0, 0):
        raise ValueError(f"Stale Windows file version {actual}: {binary}. Use a clean build.")


def stage(build, destination, inno_license):
    destination.mkdir(parents=True)
    windows = destination / "Windows"
    windows.mkdir()
    for title, target, category in (
        ("Instrument", "NeedleInstrument", "Instrument"),
        ("FX", "NeedleScratch", "Fx"),
    ):
        name = f"SKRATCH.EXE {title}.vst3"
        bundle = build / f"{target}_artefacts/Release/VST3" / name
        info = module_info(bundle)
        if info["Version"] != "0.9.0" or category not in info["Classes"][0]["Sub Categories"]:
            raise ValueError(f"Wrong release version or category for {bundle}")
        check_windows_version(bundle / "Contents/x86_64-win" / name)
        copytree(bundle, windows / name)
    standalone = build / "NeedleScratch_artefacts/Release/Standalone/SKRATCH.exe"
    check_windows_version(standalone)
    copy2(standalone, windows)

    test_exe = build / "AudioFunctionalTests_artefacts/Release/AudioFunctionalTests.exe"
    renders = destination.parent / (destination.name + "-audio-test")
    result = run([str(test_exe), str(renders)], capture_output=True, check=True)
    (destination.parent / (destination.name + "-audio-tests.csv")).write_bytes(result.stdout)
    demo = destination / "Demo"
    demo.mkdir()
    wav = demo / "Scratch-Demo.wav"
    copy2(renders / "00-dry.wav", wav)
    with wave.open(str(wav)) as recording:
        if recording.getframerate() != 48000 or recording.getnchannels() != 2:
            raise ValueError("Unexpected synthetic demo WAV format")

    docs = destination / "Docs"
    docs.mkdir()
    for name in DOCUMENTS:
        copy2(ROOT / name, docs)
    copy2(ROOT / "Installer/Installer-Readme.txt", destination)
    tools = destination / "Tools"
    tools.mkdir()
    copy2(ROOT / "Tools/Export-BetaDiagnostics.ps1", tools)
    licences = destination / "Licenses"
    copytree(ROOT / "Assets/Licenses", licences / "Fonts")
    for name in ("LICENSE.txt", "JUCE-LICENSE.md", "VST3-LICENSE.txt"):
        copy2(ROOT / name, licences)
    copy2(inno_license, licences / "Inno-Setup-LICENSE.txt")
    with zipfile.ZipFile(ROOT / "Dependencies/JUCE-8.0.13.zip") as juce:
        for name, suffix in JUCE_NOTICES.items():
            entries = [entry for entry in juce.namelist() if entry.endswith(suffix)]
            if len(entries) != 1:
                raise ValueError(f"Missing or ambiguous JUCE notice: {suffix}")
            (licences / name).write_bytes(juce.read(entries[0]))


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--inno-license", type=Path, required=True)
    args = parser.parse_args()
    stage(args.build.resolve(), args.output.resolve(), args.inno_license.resolve())
