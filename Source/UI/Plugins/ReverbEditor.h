#pragma once

#include "../../PCH.h"
#include "../../Shared/CVerb/CVerbView.h"

namespace cdaw
{
    class ReverbPlugin;

    /** C-Verb's window in CDAW: the shared CVerbView (also used by the VST3 build) connected to
        the built-in plugin's parameters, engine and undo. */
    class ReverbEditor : public te::Plugin::EditorComponent,
                         public CVerbModel
    {
    public:
        explicit ReverbEditor (ReverbPlugin&);
        ~ReverbEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }
        void resized() override;

        /** For tests and snapshots: see CVerbView. */
        void tick (float dry, float wet)        { view->tick (dry, wet); }
        void updateSpectrum()                   { view->updateSpectrum(); }

    private:
        std::shared_ptr<KnobParameter> parameter (CVerbParameters::ID) override;
        ReverbEngine& getEngine() override;
        void beginUndoTransaction (const juce::String&) override;

        ReverbPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        std::vector<std::shared_ptr<KnobParameter>> parameters;
        std::unique_ptr<CVerbView> view;
        juce::ComponentBoundsConstrainer constrainer;
    };
}
