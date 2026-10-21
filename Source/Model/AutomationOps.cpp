#include "AutomationOps.h"
#include "EditViewState.h"

namespace cdaw::AutomationOps
{
    juce::String getKey (te::AutomatableParameter& p)
    {
        return p.getOwnerID().toString() + ":" + p.paramID;
    }

    juce::Array<te::AutomatableParameter*> getAutomatableParameters (te::Track& track)
    {
        juce::Array<te::AutomatableParameter*> params;

        // Track-level and plugin parameters (volume, pan, sends and every plugin parameter).
        for (auto p : track.getAllAutomatableParams())
            params.addIfNotAlreadyThere (p);

        return params;
    }

    te::AutomatableParameter* findParameter (te::Track& track, const juce::String& key)
    {
        for (auto p : getAutomatableParameters (track))
            if (getKey (*p) == key)
                return p;

        return nullptr;
    }

    juce::String getDisplayName (te::AutomatableParameter& p)
    {
        if (auto plugin = p.getPlugin())
        {
            if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr)
                return p.getParameterName();

            return plugin->getName() + ": " + p.getParameterName();
        }

        return p.getParameterName();
    }

    juce::StringArray getLaneKeys (const te::Track& t)
    {
        juce::StringArray keys;
        keys.addLines (t.state[ViewIDs::cdawAutoLanes].toString());
        keys.removeEmptyStrings();
        return keys;
    }

    void setLaneKeys (te::Track& t, const juce::StringArray& keys)
    {
        t.state.setProperty (ViewIDs::cdawAutoLanes, keys.joinIntoString ("\n"), nullptr);
    }

    void showLane (te::Track& t, te::AutomatableParameter& p)
    {
        auto keys = getLaneKeys (t);
        keys.addIfNotAlreadyThere (getKey (p));
        setLaneKeys (t, keys);
    }

    void hideLane (te::Track& t, const juce::String& key)
    {
        auto keys = getLaneKeys (t);
        keys.removeString (key);
        setLaneKeys (t, keys);
    }

    te::AutomatableParameter* getVolumeParameter (te::Track& t)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (&t))
            if (auto vp = at->getVolumePlugin())
                return vp->volParam.get();

        if (auto ft = dynamic_cast<te::FolderTrack*> (&t))
            if (auto vp = ft->getVolumePlugin())
                return vp->volParam.get();

        return nullptr;
    }

    float toProportion (te::AutomatableParameter& p, float value)
    {
        auto r = p.getValueRange();
        return r.getLength() > 0.0f ? juce::jlimit (0.0f, 1.0f, (value - r.getStart()) / r.getLength()) : 0.0f;
    }

    float fromProportion (te::AutomatableParameter& p, float proportion)
    {
        auto r = p.getValueRange();
        return r.getStart() + juce::jlimit (0.0f, 1.0f, proportion) * r.getLength();
    }

    juce::String getModeName (te::AutomationMode m)
    {
        switch (m)
        {
            case te::AutomationMode::read:  return "Read";
            case te::AutomationMode::touch: return "Touch";
            case te::AutomationMode::latch: return "Latch";
            case te::AutomationMode::write: return "Write";
        }

        return "Read";
    }
}
