#include "LeadPlugin.h"
#include "../Shared/Lead/LeadView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* LeadPlugin::xmlTypeName = "cdawLead";

    LeadPlugin::LeadPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, LeadParameters::all(), true) {}

    LeadPlugin::~LeadPlugin()
    {
        notifyListenersOfDeletion();
    }

    double LeadPlugin::getTailLength() const
    {
        const auto s = getSettings();
        double tail = s.ampEnv.release;
        if (s.reverbOn)
            tail += s.reverbDecay * 1.1;
        if (s.delayOn)
            tail += DelayEngine::getTailSeconds (LeadEngine::delaySettings (s), getBpmNow());
        return juce::jmin (30.0, tail);
    }

    void LeadPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
        midi.ensureSize (4096);
    }

    void LeadPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
            return;

        const int start = fc.bufferStartSample, n = fc.bufferNumSamples;
        midi.clear();

        if (auto* messages = fc.bufferForMidiMessages)
        {
            if (messages->isAllNotesOff)
                engine.allNotesOff();

            // As Tracktion's own synths read them: clamped into the block, so one that rounds up to its end isn't lost.
            for (auto& m : *messages)
            {
                const int at = juce::jlimit (start, start + n - 1, juce::roundToInt (m.getTimeStamp() * sampleRate));
                midi.addEvent (m, at - start);
            }
        }

        engine.process (*fc.destBuffer, start, n, midi, getSettings(), getBpm (fc), getSongPosition (fc));
    }

    std::unique_ptr<te::Plugin::EditorComponent> LeadPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<LeadView, LeadEngine>> (*this, engine);
    }
}
