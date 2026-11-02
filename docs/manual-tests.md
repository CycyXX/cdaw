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
- [ ] Loop-record 4 passes over 4 bars: the clip shows 4 takes, the lanes open and the last complete pass plays.
- [ ] Record 8 bars, then record bars 3-4 again without looping. While recording you hear the new pass and not the
      old one; afterwards both are on the lanes, and the old pass plays either side of the new one.
- [ ] Punch in on a recorded part: the lead-in plays the old take, the punch range doesn't. Both end up as takes.
- [ ] Loopback cable from output 3 to input 2: record the click and zoom in. It should line up
      within about 1 ms. If not, set Preferences > Recording > Manual offset.

## M3: plugins
- [ ] Plugins > Scan: Nolly X, Plini X, Parallax X, SSDSampler5, GGD Modern and Massive 2 appear.
- [ ] Put Nolly X on the DI track; monitor at 64 samples; latency feels playable.
- [ ] Nolly X GUI at 100 %, 125 % and 150 % Windows scaling; move it between monitors.
- [ ] Save and reopen: the Nolly X preset is restored.

## M4: MIDI
- [ ] USB keyboard into an SSD5 instrument track: hits play with low latency.
- [ ] Record 8 bars, then record over bars 3-4: the first pass goes quiet while recording and stays as a take.
      Lanes show both passes as notes. Ctrl+Down / Ctrl+Up on the clip switch takes, and swiping a lane comps them.
- [ ] Preferences > Recording > Recording MIDI over a clip > Overdub: loop-overdub hi-hats into the kit.
- [ ] Drum editor: rows match SSD5's pieces (edit resources\drummaps\SSD5.txt if not).
- [ ] Quantize to 1/16. Export a .mid and open it elsewhere.
- [ ] teVirtualMIDI ports do not cause doubled notes.

## M5: editing and comping
- [ ] Split, trim, fade, crossfade and move clips; everything plays without clicks.
- [ ] Record 4 takes of a riff, swipe-comp the best parts, playback is seamless, then flatten.
- [ ] Open a project with a drum kit split over many tracks: select them, Ctrl+Shift+G. In the new folder (G lit):
      click one clip and all the lined-up clips select. Drag, trim, fade, nudge, mute, delete and split one: all
      follow and stay in phase. A range over one track covers the whole group. Click G on the folder (or Ctrl+G) to
      edit one track alone.
- [ ] Multi-mic recording of the kit, twice over the same bars: only the first track opens its lanes. Swipe there:
      every drum track switches at the same place, no phase jump at the joins.
- [ ] Add a tempo change mid-song and a 7/8 bar: MIDI follows, audio keeps its position.

## M6: mixer
- [ ] Two rhythm guitars into a "Guitars" group; a reverb FX channel fed by sends.
- [ ] SSD5 multi-out: kick, snare, toms and overheads on their own mixer channels.
- [ ] Master meter and CPU meter behave.

## M7: automation
- [ ] Latch-automate the Nolly X gain from its own GUI during playback; edit the points afterwards.

## M8: songwriting
- [ ] Open Night Drive v2, File > Save New Version (Ctrl+Alt+S) with a note: the title shows `Night Drive v2_02`.
      File > Versions lists both with their notes; open the first one: it is unchanged.
- [ ] F4: write song notes and lyrics for two sections. Play: the lyric box of the section playing lights up.
      Alt-drag a section to copy it: the copy has the lyrics. Save, reopen: everything is there.
- [ ] Set the key to A Minor in the transport bar; open a MIDI clip: notes outside A minor are shaded.
      Turn on Snap to Key, draw and drag notes, press Up / Down: they stay in the key.
- [ ] Build a "Metal demo": SSD5 track, four guitar DIs on Nolly / Plini in a "Guitars" group, bass, a reverb
      FX return with sends. File > Save as Template, then File > New from Template: all of it is there, the
      plugins with their presets, and no clips.
- [ ] Right-click the Nolly track > Save as Track Preset; in another song with an FX track of the same name,
      Insert Track Preset: the send reaches that FX track.

## M9: export
- [ ] Export the song as 24-bit / 48 kHz WAV.
- [ ] Export stems; import them into another DAW at bar 1; they line up.
- [ ] Export MP3 (needs libmp3lame.dll, see README).
