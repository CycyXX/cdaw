#include "MultibandEditor.h"
#include "TeKnobParameter.h"
#include "../../Plugins/MultibandPlugin.h"

namespace cdaw
{
    MultibandEditor::MultibandEditor (MultibandPlugin& p) : plugin (p), pluginRef (&p)
    {
        for (int i = 0; i < MultibandParameters::count; ++i)
            parameters.push_back (knobParameter (plugin.parameter (i).getParameter()));

        view = std::make_unique<MultibandView> (*this);
        addAndMakeVisible (*view);

        constrainer.setMinimumSize (MultibandView::minimumWidth, MultibandView::minimumHeight);
        setSize (MultibandView::defaultWidth, MultibandView::defaultHeight);
    }

    MultibandEditor::~MultibandEditor()
    {
        view.reset();
    }

    void MultibandEditor::resized()
    {
        view->setBounds (getLocalBounds());
    }

    std::shared_ptr<KnobParameter> MultibandEditor::parameter (int index)
    {
        return parameters[(size_t) index];
    }

    MultibandEngine& MultibandEditor::getEngine()
    {
        return plugin.engine;
    }

    void MultibandEditor::beginUndoTransaction (const juce::String& name)
    {
        plugin.edit.getUndoManager().beginNewTransaction (name);
    }
}
