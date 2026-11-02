#include "SynthPlugin.h"
#include "../Shared/Synth/SynthView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* SynthPlugin::xmlTypeName = "cdawSynth";

    SynthPlugin::SynthPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, SynthParameters::all(), true) {}

    SynthPlugin::~SynthPlugin()
    {
        notifyListenersOfDeletion();
    }

    double SynthPlugin::getTailLength() const
    {
        const auto s = getSettings();
        double tail = s.ampEnv.release;
        if (s.reverbOn)
            tail += s.reverbDecay * 1.1;
        if (s.delayOn)
        {
            DelayEngine::Settings d;
            d.sync = true;
            d.division = s.delayDivision;
            d.feedback = s.delayFeedback;
            d.mode = DelayEngine::Mode::pingPong;
            tail += DelayEngine::getTailSeconds (d, getBpmNow());
        }
        return juce::jmin (30.0, tail);
    }

    void SynthPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
        midi.ensureSize (4096);
    }

    void SynthPlugin::applyToBuffer (const te::PluginRenderContext& fc)
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

    std::unique_ptr<te::Plugin::EditorComponent> SynthPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<SynthView, SynthEngine>> (*this, engine);
    }
}
