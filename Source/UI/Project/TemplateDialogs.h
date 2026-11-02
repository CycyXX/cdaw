#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** The small dialogs and menus around project templates and track presets. */
    namespace TemplateDialogs
    {
        /** Asks for a name, then calls `write` with <folder>/<name><suffix> (after asking before
            replacing one that exists). `done` gets the file written, or {} if it failed. */
        void askNameAndSave (juce::Component* parent, const juce::String& title, const juce::String& defaultName,
                             const juce::File& folder, const char* suffix,
                             std::function<bool (const juce::File&)> write,
                             std::function<void (const juce::File&)> done);

        /** Saves the track (and what's in it) as a track preset, reporting in the status bar. */
        void saveTrackPreset (juce::Component* parent, te::Track&);

        /** The presets, inserted after `after` (or at the end) when picked, plus "Open Folder". */
        juce::PopupMenu createInsertTrackPresetMenu (te::Edit&, te::Track* after, te::SelectionManager*);
    }
}
