#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Import and export of DAWproject files (.dawproject): the open exchange format read and
        written by Cubase 14+, Bitwig Studio, Studio One and others.
        Spec: https://github.com/bitwig/dawproject (format version 1.0).

        A .dawproject is a zip with project.xml (structure, mixer, timeline), metadata.xml and
        the media: audio files and plugin states (VST3 as .vstpreset).

        What travels both ways:
          - tracks (audio, instrument, FX returns, group / folder tracks), names, colours
          - volume, pan, mute, solo, output to groups, pre/post sends to FX tracks
          - audio clips (offset, fades), MIDI clips (notes, loop range)
          - VST3 plugins with their full state; the built-in Compressor and 8-band EQ as the
            format's generic Compressor / Equalizer devices
          - volume, pan and plugin parameter automation
          - tempo and time signature changes, markers (song sections are written as markers)
        Anything that doesn't translate is listed in the Report instead of disappearing silently. */
    namespace DawProject
    {
        struct Report
        {
            juce::StringArray notes;        // things that didn't translate exactly
            int tracks = 0, clips = 0, plugins = 0, automationLanes = 0, markers = 0;

            void note (const juce::String& message)     { notes.addIfNotAlreadyThere (message); }
            juce::String summary() const;
        };

        static constexpr const char* fileExtension = ".dawproject";

        /** Writes the edit (with its audio and plugin states embedded) to `destination`. */
        juce::Result exportTo (te::Edit&, const juce::File& destination, Report&);

        /** Replaces the contents of `edit` with the project in `source`. Audio files are
            extracted into `mediaFolder` (e.g. the project's Audio/Imported folder). */
        juce::Result importInto (te::Edit&, const juce::File& source, const juce::File& mediaFolder, Report&);
    }
}
