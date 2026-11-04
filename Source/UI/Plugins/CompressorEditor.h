#pragma once

#include "../../PCH.h"
#include "Shared/Compressor/CompressorView.h"

namespace cdaw
{
    class CompressorPlugin;

    /** C-Comp's window in CDAW: the shared CompressorView (also used by the VST3 build) connected
        to the built-in plugin's parameters, engine, undo and sidechain routing. */
    class CompressorEditor : public te::Plugin::EditorComponent,
                             public CompressorModel
    {
    public:
        explicit CompressorEditor (CompressorPlugin&);
        ~CompressorEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }
        void resized() override;

        /** For tests and snapshots: see CompressorView. */
        void tick (float inL, float inR, float outL, float outR, float detector, float reductionDb)
        {
            view->tick (inL, inR, outL, outR, detector, reductionDb);
        }

    private:
        std::shared_ptr<KnobParameter> parameter (CompressorParameters::ID) override;
        CompressorEngine& getEngine() override;
        void beginUndoTransaction (const juce::String&) override;
        juce::String getSidechainSource() override;
        bool canChooseSidechain() const override                            { return true; }
        void showSidechainMenu (juce::Component&) override;

        CompressorPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        std::vector<std::shared_ptr<KnobParameter>> parameters;
        std::unique_ptr<CompressorView> view;
        juce::ComponentBoundsConstrainer constrainer;
    };
}
