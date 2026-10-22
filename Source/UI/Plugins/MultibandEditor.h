#pragma once

#include "../../PCH.h"
#include "../../Shared/Multiband/MultibandView.h"

namespace cdaw
{
    class MultibandPlugin;

    /** C-Multicomp's window in CDAW: the shared MultibandView (also used by the VST3 build)
        connected to the built-in plugin's parameters, engine and undo. */
    class MultibandEditor : public te::Plugin::EditorComponent,
                            public MultibandModel
    {
    public:
        explicit MultibandEditor (MultibandPlugin&);
        ~MultibandEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }
        void resized() override;

        /** For tests and snapshots: see MultibandView. */
        using Levels = MultibandView::Levels;
        void tick (const Levels& levels)        { view->tick (levels); }
        void updateSpectrum()                   { view->updateSpectrum(); }

    private:
        std::shared_ptr<KnobParameter> parameter (int index) override;
        MultibandEngine& getEngine() override;
        void beginUndoTransaction (const juce::String&) override;

        MultibandPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        std::vector<std::shared_ptr<KnobParameter>> parameters;
        std::unique_ptr<MultibandView> view;
        juce::ComponentBoundsConstrainer constrainer;
    };
}
