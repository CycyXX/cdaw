# CDAW

A native Windows DAW for recording, MIDI production, editing and mixing.
Built in C++20 on [JUCE](https://juce.com) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine).

- ASIO and Windows Audio (WASAPI) devices; ASIO is picked by default when present.
- VST3 instruments and effects, scanned out of process so a crashing plugin cannot take the app down.
- Audio and MIDI recording with punch in/out, count-in, metronome and loop takes with swipe comping.
- Arrangement editing, piano roll and drum editor, mixer with inserts, sends, groups and FX channels.
- Automation lanes, tempo and time-signature changes, markers.
- Export to WAV, FLAC, OGG and MP3, stems, freeze.

## Status

Built and verified with automated tests (95 checks) and a launch test on the UR44C:

- ASIO device selection (Yamaha Steinberg USB first), mono inputs, preferences, device status in the status bar.
- Arrangement: tracks, clips, waveforms, drag-drop import, move/copy/trim/fade/split/duplicate, snap grid, markers, tempo and time-signature changes, undo (200 steps), autosave and crash recovery.
- Recording plumbing: record arm, input choice, monitor modes, click, count-in, punch, loop recording into takes, take lanes with swipe comping.
- VST3 hosting with out-of-process scanning, insert slots, sends and FX tracks, groups, master strip, mixer.
- MIDI: USB inputs, instrument tracks, piano roll, drum editor with maps, MIDI import and export.
- Automation lanes (draw, edit, bend; Read/Touch/Latch/Write).
- Export: mixdown and stems to WAV/FLAC/OGG (MP3 with libmp3lame.dll), MIDI files.

**Not yet verified on real hardware** (needs you at the UR44C): live recording and monitoring latency, loop-take capture, USB keyboard into SSD5/GGD, multi-output drum routing, Neural DSP GUIs at your scaling. Use `docs\manual-tests.md`.

**Not built:** multi-output instrument routing into separate mixer channels, MIDI learn, CC lanes in the piano roll, audio time-stretch UI, clip launcher, control surfaces.

## Licence

CDAW links Tracktion Engine (GPLv3) and JUCE 8 (AGPLv3). That is fine for personal use.
If you ever distribute CDAW, you must publish its source under a compatible licence or buy
commercial licences for both.

## Prerequisites (one time)

```powershell
.\scripts\setup-toolchain.ps1   # MSVC Build Tools 2022, CMake, Ninja (accept the admin prompt)
.\scripts\bootstrap.ps1         # git init + Tracktion Engine (pinned) + JUCE submodule
```

You do not need the Visual Studio IDE. The Build Tools package is only the compiler and the Windows SDK.

## Build and run

```powershell
.\scripts\build.ps1                         # RelWithDebInfo (daily build)
.\scripts\build.ps1 -Test                   # build and run the automated tests
.\scripts\build.ps1 -Preset ninja-msvc-debug
.\build\ninja-msvc-relwithdebinfo\CDAW_artefacts\RelWithDebInfo\CDAW.exe
```

The first build compiles all of JUCE and Tracktion and takes a while. Later builds are incremental.
VS Code with the CMake Tools extension picks up `CMakePresets.json` directly.

## Where things live

| What | Where |
|---|---|
| App settings, key mappings, recent projects | `%APPDATA%\CDAW\CDAW.settings` |
| Engine settings, audio device, plugin list | `%APPDATA%\CDAW\Settings.xml` |
| Logs | `%APPDATA%\CDAW\Logs` |
| Crash reports | `%APPDATA%\CDAW\Crashes` |
| Your drum maps | `%APPDATA%\CDAW\DrumMaps\*.txt` |
| Projects (default) | `Documents\CDAW Projects\<Song>\` |

A project is a folder holding `<Song>.tracktionedit`, an `Audio\` folder with recordings and
imported files, `Bounces\`, `MIDI\`, `Backups\` and a `Cache\` folder that is safe to delete.

## MP3 export

MP3 encoding uses LAME, loaded at runtime. Put `libmp3lame.dll` (64-bit) next to `CDAW.exe`
to enable the MP3 option in the export dialog.

## Updating Tracktion Engine

The engine is pinned in `scripts\bootstrap.ps1`. To update, change the commit there, rerun the
script, read `third_party\tracktion_engine\BREAKING-CHANGES.md`, rebuild and run the tests.

## Troubleshooting

- **No ASIO device / device busy**: close other DAWs. Only one app can usually use an ASIO driver.
- **Doubled dry guitar while monitoring**: turn off direct monitoring in dspMixFx.
- **A plugin crashed CDAW**: on the next start CDAW offers to disable it. Re-enable it in
  Preferences > Plugins.
- **Crackles at 64 samples**: use the RelWithDebInfo or Release build, the High Performance power
  plan, and turn off USB selective suspend.
