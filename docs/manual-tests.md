# CDAW manual hardware checks

Automated tests (`scripts\build.ps1 -Test`) cover the engine side. These checks need your
hands, your Steinberg UR44C and your plugins. Run them with the RelWithDebInfo build;
Debug is too slow for real-time work at small buffer sizes.

Before you start:
- Close Cubase or any other app that uses the UR44C ASIO driver.
- In dspMixFx, turn **off** direct monitoring, otherwise you hear the dry DI as well.
- Use the High Performance power plan and turn off USB selective suspend.

## M0: engine and audio device
- [ ] CDAW starts on ASIO with "Yamaha Steinberg USB ASIO" at 48 kHz.
- [ ] Options > Audio Settings shows the device; "Show ASIO control panel" opens the Yamaha panel.
- [ ] Switch to Windows Audio (WASAPI) and back; restart CDAW; the last choice is kept.
- [ ] Debug > Open plugin from file: Archetype Nolly X opens and its GUI looks sharp.

## M1: arrangement and files
- [ ] Drag a WAV, a FLAC and an MP3 from Explorer into the arrangement; each becomes a clip.
- [ ] Play: audio comes out of UR44C main outs 1/2. Loop a region with the ruler.
- [ ] Save, close, move the project folder elsewhere, reopen: all clips still play.
- [ ] Undo 20 steps and redo them.

## M2: recording
- [ ] Guitar DI into Hi-Z input 1. Create an audio track, input "In 1", arm it: you hear the DI.
- [ ] Record 8 bars with the click and a 1-bar count-in.
- [ ] Punch in on bars 5-6 only (punch on, loop range bars 5-6).
- [ ] Loop-record 4 passes over 4 bars: the clip shows 4 takes.
- [ ] Loopback cable from output 3 to input 2: record the click and zoom in. It should line up
      within about 1 ms. If not, set Preferences > Recording > Manual offset.

## M3: plugins
- [ ] Plugins > Scan: Nolly X, Plini X, Parallax X, SSDSampler5, GGD Modern and Massive 2 appear.
- [ ] Put Nolly X on the DI track; monitor at 64 samples; latency feels playable.
- [ ] Nolly X GUI at 100 %, 125 % and 150 % Windows scaling; move it between monitors.
- [ ] Save and reopen: the Nolly X preset is restored.

## M4: MIDI
- [ ] USB keyboard into an SSD5 instrument track: hits play with low latency.
- [ ] Record 8 bars, then loop-overdub hi-hats.
- [ ] Drum editor: rows match SSD5's pieces (edit resources\drummaps\SSD5.txt if not).
- [ ] Quantize to 1/16. Export a .mid and open it elsewhere.
- [ ] teVirtualMIDI ports do not cause doubled notes.

## M5: editing and comping
- [ ] Split, trim, fade, crossfade and move clips; everything plays without clicks.
- [ ] Record 4 takes of a riff, swipe-comp the best parts, playback is seamless, then flatten.
- [ ] Add a tempo change mid-song and a 7/8 bar: MIDI follows, audio keeps its position.

## M6: mixer
- [ ] Two rhythm guitars into a "Guitars" group; a reverb FX channel fed by sends.
- [ ] SSD5 multi-out: kick, snare, toms and overheads on their own mixer channels.
- [ ] Master meter and CPU meter behave.

## M7: automation
- [ ] Latch-automate the Nolly X gain from its own GUI during playback; edit the points afterwards.

## M8: export
- [ ] Export the song as 24-bit / 48 kHz WAV.
- [ ] Export stems; import them into another DAW at bar 1; they line up.
- [ ] Export MP3 (needs libmp3lame.dll, see README).
