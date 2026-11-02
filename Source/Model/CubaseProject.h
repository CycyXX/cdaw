#pragma once

#include "../PCH.h"
#include "DawProject.h"

namespace cdaw
{
    /** Import of Cubase / Nuendo projects (.cpr), reverse engineered from Cubase 10.5 projects
        (see CubaseArchive.h for the file format).

        The project is translated into a DAWproject (project.xml plus .vstpreset plugin states, in
        memory) and handed to DawProject's importer, so both share the mapping onto an Edit.

        What comes across:
          - audio, instrument and MIDI tracks, folders, group and FX channels, names
          - audio events (offset, length, fades, event volume) on the audio files in the project's
            Audio folder; where events overlap, only the part on top plays (as in Cubase)
          - MIDI parts: notes, controllers, pitch bend, channel pressure
          - volume, pan, mute, output routing to groups, sends to FX channels (pre / post)
          - insert and instrument plugins (VST3) with their saved state, when installed; Cubase's
            own effects as CDAW's built-in ones where there is one (see CubaseStockPlugins)
          - tempo and time signature changes, markers
        Folders that hold a group channel (with the tracks feeding it) become CDAW groups. */
    namespace CubaseProject
    {
        static constexpr const char* fileExtension = ".cpr";

        struct ImportOptions
        {
            /** Copy the audio files into the media folder, or use them where they are. */
            bool copyAudio = true;

            /** Cubase's own effects are replaced by CDAW's where they sound the same (Frequency by
                the EQ, Compressor by C-Comp); with this, also where they only sound similar
                (limiters, tube / vintage compressors, the de-esser...). See CubaseStockPlugins. */
            bool matchSimilarPlugins = false;

            /** Called along the way: 0..1, what's happening. */
            std::function<void (double, const juce::String&)> progress;
        };

        /** Replaces the contents of `edit` with the Cubase project in `source`. The audio files it
            uses are copied into `mediaFolder` (e.g. the project's Audio/Imported folder). */
        juce::Result importInto (te::Edit&, const juce::File& source, const juce::File& mediaFolder, DawProject::Report&,
                                 const ImportOptions& = {});

        /** The project as a DAWproject document: project.xml and the files it refers to (plugin
            states). Audio files are referenced where they are, as external files. */
        struct Translation
        {
            std::unique_ptr<juce::XmlElement> project;
            std::vector<std::pair<juce::String, juce::MemoryBlock>> files;      // path in the zip -> data
        };

        juce::Result translate (const juce::File& source, Translation&, DawProject::Report&, bool matchSimilarPlugins = false);

        /** Fader position (0..32767, as stored by Cubase) to dB; -inf below the bottom. */
        double faderToDb (double position);

        /** An outline of the file's object tree, for reverse engineering. */
        juce::String describe (const juce::File& source, int maxDepth = 8);
    }
}
