#pragma once

#include "../PCH.h"

namespace cdaw::AutomationOps
{
    /** Stable key for a parameter: "<owner item id>:<param id>". */
    juce::String getKey (te::AutomatableParameter&);
    te::AutomatableParameter* findParameter (te::Track&, const juce::String& key);

    /** Parameters of a track and its plugins, for the lane picker. */
    juce::Array<te::AutomatableParameter*> getAutomatableParameters (te::Track&);
    juce::String getDisplayName (te::AutomatableParameter&);

    /** Automation lanes shown under a track (stored on the track, no undo). */
    juce::StringArray getLaneKeys (const te::Track&);
    void setLaneKeys (te::Track&, const juce::StringArray&);
    void showLane (te::Track&, te::AutomatableParameter&);
    void hideLane (te::Track&, const juce::String& key);

    /** The track's volume parameter (for the default lane). */
    te::AutomatableParameter* getVolumeParameter (te::Track&);

    /** Parameter value <-> 0..1 for drawing. */
    float toProportion (te::AutomatableParameter&, float value);
    float fromProportion (te::AutomatableParameter&, float proportion);

    juce::String getModeName (te::AutomationMode);
}
