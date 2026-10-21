#include "ReverbEditor.h"
#include "TeKnobParameter.h"
#include "../../Plugins/ReverbPlugin.h"

namespace cdaw
{
    ReverbEditor::ReverbEditor (ReverbPlugin& p) : plugin (p), pluginRef (&p)
    {
        for (int i = 0; i < CVerbParameters::count; ++i)
            parameters.push_back (knobParameter (plugin.parameter ((CVerbParameters::ID) i).getParameter()));

        view = std::make_unique<CVerbView> (*this);
        addAndMakeVisible (*view);

        constrainer.setMinimumSize (CVerbView::minimumWidth, CVerbView::minimumHeight);
        setSize (CVerbView::defaultWidth, CVerbView::defaultHeight);
    }

    ReverbEditor::~ReverbEditor()
    {
        view.reset();
    }

    void ReverbEditor::resized()
    {
        view->setBounds (getLocalBounds());
    }

    std::shared_ptr<KnobParameter> ReverbEditor::parameter (CVerbParameters::ID id)
    {
        return parameters[(size_t) id];
    }

    ReverbEngine& ReverbEditor::getEngine()
    {
        return plugin.engine;
    }

    void ReverbEditor::beginUndoTransaction (const juce::String& name)
    {
        plugin.edit.getUndoManager().beginNewTransaction (name);
    }
}
