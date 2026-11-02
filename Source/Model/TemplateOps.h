#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Project templates and track presets: the set-up of a song without its content.

        A template (Templates\<Name>.cdawtemplate) is a whole project: tracks, folders and groups,
        routing, sends and FX returns, plugins with their settings, inputs, tempo and signature.
        A track preset (TrackPresets\<Name>.cdawtrack) is one track, or a folder with the tracks in
        it, with its plugins and sends. Both leave out clips, markers, song sections, automation,
        freezes and the song's notes. Both live in %APPDATA%\CDAW. */
    namespace TemplateOps
    {
        inline constexpr const char* templateSuffix = ".cdawtemplate";
        inline constexpr const char* trackPresetSuffix = ".cdawtrack";

        juce::File getTemplatesFolder();
        juce::File getTrackPresetsFolder();

        /** The .cdawtemplate / .cdawtrack files in a folder, sorted by name. */
        juce::Array<juce::File> listFiles (const juce::File& folder, const char* suffix);

        /** Removes everything song-specific from an edit or track state (in place). */
        void stripContent (juce::ValueTree&);

        /** Writes the edit's set-up as a template. */
        bool saveTemplate (te::Edit&, const juce::File& destination);

        /** Writes a template's contents as a new project file (which is then opened as usual). */
        bool createProjectFromTemplate (const juce::File& templateFile, const juce::File& editFile);

        /** Writes the track (with any tracks inside it) as a track preset. */
        bool saveTrackPreset (te::Track&, const juce::File& destination);

        /** Inserts a track preset after `after` (or at the end). Returns the new top track. */
        te::Track::Ptr insertTrackPreset (te::Edit&, const juce::File& preset, te::Track* after, te::SelectionManager*);
    }
}
