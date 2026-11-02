#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Shared/Common/EffectModel.h"
#include "Shared/Common/ParameterSpec.h"
#include "Shared/DSP/TempoSync.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"

namespace cdaw
{
    /** The VST3 host glue shared by C-Delay, C-Chorus, C-Phaser and C-Filter: one host parameter per
        spec of the plugin's shared list (the same IDs, ranges and text as in CDAW), saved and
        restored with the host's project, the host's tempo for synced times and rates, and mono
        or stereo buses. The sound and the editor are the shared engine and view. */
    class SharedEffectProcessor : public juce::AudioProcessor
    {
    public:
        /** `isSynth`: an instrument (MIDI in, no audio in, stereo out). */
        SharedEffectProcessor (const juce::String& pluginName, const juce::String& stateName, const ParameterSpecs& s, bool stereoOut,
                               bool isSynth = false)
            : juce::AudioProcessor (isSynth ? BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                            : BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                               .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
              name (pluginName), specs (s), stereoOutput (stereoOut), synth (isSynth),
              state (*this, nullptr, juce::Identifier (stateName), createLayout (s))
        {
            for (auto& spec : specs)
            {
                parameters.push_back (state.getParameter (spec.id));
                values.push_back (state.getRawParameterValue (spec.id));
                jassert (parameters.back() != nullptr && values.back() != nullptr);
            }
        }

        const juce::String getName() const override             { return name; }
        bool acceptsMidi() const override                       { return synth; }
        bool producesMidi() const override                      { return false; }
        double getTailLengthSeconds() const override            { return 0.0; }
        void releaseResources() override                        {}
        bool hasEditor() const override                         { return true; }

        bool isBusesLayoutSupported (const BusesLayout& layouts) const override
        {
            // Mono or stereo in; stereo out for the stereo effects, else the same as in.
            const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
            if (synth)
                return in.isDisabled() && out == juce::AudioChannelSet::stereo();

            const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
            return inOk && (stereoOutput ? out == juce::AudioChannelSet::stereo() : out == in);
        }

        int getNumPrograms() override                           { return 1; }
        int getCurrentProgram() override                        { return 0; }
        void setCurrentProgram (int) override                   {}
        const juce::String getProgramName (int) override        { return "Default"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override
        {
            if (auto xml = state.copyState().createXml())
                copyXmlToBinary (*xml, destData);
        }

        void setStateInformation (const void* data, int sizeInBytes) override
        {
            if (auto xml = getXmlFromBinary (data, sizeInBytes))
                if (xml->hasTagName (state.state.getType()))
                    state.replaceState (juce::ValueTree::fromXml (*xml));
        }

        //==============================================================================
        float value (int id) const                              { return values[(size_t) id]->load(); }
        std::function<float (int)> valueOf() const              { return [this] (int id) { return value (id); }; }
        juce::RangedAudioParameter& getHostParameter (int id)   { return *parameters[(size_t) id]; }
        const ParameterSpec& getSpec (int id) const             { return specs[(size_t) id]; }
        int getNumSharedParameters() const                      { return (int) specs.size(); }

        /** Reads the host's play head at the start of a block: its tempo (the last one known
            when it gives none) and where the song is. */
        TempoSync::SongPosition readPlayHead()
        {
            TempoSync::SongPosition song;

            if (auto* head = getPlayHead())
                if (auto position = head->getPosition())
                {
                    if (auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                        lastBpm = *bpm;

                    song.playing = position->getIsPlaying();
                    song.seconds = position->getTimeInSeconds().orFallback (0.0);
                    song.beats = position->getPpqPosition().orFallback (song.seconds * lastBpm.load() / 60.0);
                }

            return song;
        }

        /** The tempo the last block played at (for the editor). */
        double getLastBpm() const                               { return lastBpm.load(); }

        /** Mono in, stereo out: the second channel starts as a copy of the first. */
        void copyMonoInput (juce::AudioBuffer<float>& buffer)
        {
            if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
                buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
        }

    private:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const ParameterSpecs& specs)
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;

            for (auto& spec : specs)
            {
                auto attributes = juce::AudioParameterFloatAttributes()
                                      .withStringFromValueFunction ([toText = spec.toText] (float v, int) { return toText (v); })
                                      .withValueFromStringFunction ([fromText = spec.fromText] (const juce::String& s) { return fromText (s); });

                layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spec.id, 1 }, spec.name, spec.getRange(),
                                                                         spec.defaultValue, attributes));
            }

            return layout;
        }

        const juce::String name;
        const ParameterSpecs& specs;
        const bool stereoOutput, synth;
        std::vector<juce::RangedAudioParameter*> parameters;
        std::vector<std::atomic<float>*> values;
        std::atomic<double> lastBpm { 120.0 };

    public:
        juce::AudioProcessorValueTreeState state;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharedEffectProcessor)
    };

    //==============================================================================
    /** A host parameter for the shared knobs and views. */
    struct HostKnobParameter : public KnobParameter
    {
        HostKnobParameter (juce::RangedAudioParameter& p, const ParameterSpec& s) : param (p), spec (s) {}

        float getValue() const override                     { return param.convertFrom0to1 (param.getValue()); }
        void setValue (float v) override                    { param.setValueNotifyingHost (param.convertTo0to1 (v)); }
        void beginGesture() override                        { param.beginChangeGesture(); }
        void endGesture() override                          { param.endChangeGesture(); }
        juce::Range<float> getRange() const override        { return param.getNormalisableRange().getRange(); }
        juce::String getText (float v) const override       { return spec.toText (v); }

        juce::RangedAudioParameter& param;
        const ParameterSpec& spec;
    };

    /** The plugin window: the shared view, in CDAW's look. */
    template <typename View, typename Engine>
    class HostEffectEditor : public juce::AudioProcessorEditor,
                             public EffectModel
    {
    public:
        HostEffectEditor (SharedEffectProcessor& p, Engine& engine) : juce::AudioProcessorEditor (p), processor (p)
        {
            setLookAndFeel (&lookAndFeel);

            for (int i = 0; i < processor.getNumSharedParameters(); ++i)
                parameters.push_back (std::make_shared<HostKnobParameter> (processor.getHostParameter (i), processor.getSpec (i)));

            view = std::make_unique<View> (*this, engine);
            addAndMakeVisible (*view);

            setResizable (true, true);
            setResizeLimits (View::minimumWidth, View::minimumHeight, 2400, 1600);
            setSize (View::defaultWidth, View::defaultHeight);
        }

        ~HostEffectEditor() override
        {
            view.reset();
            setLookAndFeel (nullptr);
        }

        void resized() override                                             { view->setBounds (getLocalBounds()); }
        std::shared_ptr<KnobParameter> parameter (int id) override          { return parameters[(size_t) id]; }
        double getBpm() override                                            { return processor.getLastBpm(); }

        juce::String getText (const juce::String& key) override             { return processor.state.state[juce::Identifier (key)].toString(); }
        void setText (const juce::String& key, const juce::String& value) override
        {
            processor.state.state.setProperty (juce::Identifier (key), value, nullptr);
        }

    private:
        SharedEffectProcessor& processor;
        CDAWLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltipWindow { this };
        std::vector<std::shared_ptr<KnobParameter>> parameters;
        std::unique_ptr<View> view;
    };
}
