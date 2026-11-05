# Contributing

Bug reports, fixes and new ideas are all welcome. For anything big, open an issue first so we can agree on
the approach.

**Changing a C- plugin (C-Verb, C-Comp, C-Lead and the others)?** Those live in
[cdaw-plugins](https://github.com/CycyXX/cdaw-plugins), with their own guide. Make the change there first. CDAW
then picks it up by moving its `third_party/cdaw-plugins` submodule to the new commit. The code in this repository
that connects them to CDAW (`Source/Plugins`) belongs here.

## Sign your commits

This project uses the [Developer Certificate of Origin](https://developercertificate.org). By signing off,
you state that you wrote the change or have the right to submit it under the project's licence
(AGPL-3.0-or-later). You keep your copyright.

Add a `Signed-off-by` line with `-s`, using your real name:

```
git commit -s -m "Piano roll: keep the selection when zooming"
```

## Build and test

CDAW builds on Windows only for now. See [Getting started](README.md#getting-started) for the tools. To build
everything, including the VST3s some tests compare against, and run the tests:

```powershell
.\scripts\build.ps1 -Test
```

Please run them before opening a pull request. They need no audio device, so they can't tell you how a change behaves
with real hardware, or how it sounds. If your change touches audio, devices, recording or plugin hosting, try it with
a real project and say so in the pull request.

Every pull request also runs on GitHub, on Windows: it builds everything and runs the same tests. It has to pass before
merging. A full run can take a long time, so you don't need to wait for it; you'll see the result on the pull
request. Changes to docs alone skip the build.

## Three rules that protect people's work

**1. Old projects must keep opening and sounding the same.** Saved projects find plugins and settings by their names
and IDs, so don't rename them. C-Multicomp and C-Comp keep the type names `cdawMultiband` and `cdawCompressor` for
exactly this reason. If a project format has to change, convert old files when they're opened, as CDAW does for
`.tracktionedit`, and add a test.

**2. Say so when a change alters the sound.** Existing mixes should keep sounding the same unless there's a good
reason. Say in the pull request what changes and when.

**3. Nothing that can wait on the audio thread.** No locks, allocation or file access there.

## Style

Match the code around it: Allman braces, four spaces, a space before call parentheses, plain comments that say why.
The README's [Repository layout](README.md#repository-layout) shows where things are.
