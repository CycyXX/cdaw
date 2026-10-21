#include "CVerbProcessor.h"
#include "Shared/CVerb/CVerbView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"

namespace cdaw
{
    namespace
    {
        /** A host parameter for the shared knobs and views. */
        struct HostKnobParameter : public KnobParameter
        {
            HostKnobParameter (juce::RangedAudioParameter& p, const CVerbParameters::Spec& s) : param (p), spec (s) {}

            float getValue() const override                     { return param.convertFrom0to1 (param.getValue()); }
            void setValue (float v) override                    { param.setValueNotifyingHost (param.convertTo0to1 (v)); }
            void beginGesture() override                        { param.beginChangeGesture(); }
            void endGesture() override                          { param.endChangeGesture(); }
            juce::Range<float> getRange() const override        { return param.getNormalisableRange().getRange(); }
            juce::String getText (float v) const override       { return spec.toText (v); }

            juce::RangedAudioParameter& param;
            const CVerbParameters::Spec& spec;
        };

        /** The plugin window: the shared C-Verb view, in CDAW's look. */
        class CVerbEditor : public juce::AudioProcessorEditor,
                            public CVerbModel
        {
        public:
            explicit CVerbEditor (CVerbProcessor& p) : juce::AudioProcessorEditor (p), processor (p)
            {
                setLookAndFeel (&lookAndFeel);

                for (int i = 0; i < CVerbParameters::count; ++i)
                {
                    auto id = (CVerbParameters::ID) i;
                    parameters.push_back (std::make_shared<HostKnobParameter> (processor.getParameter (id), CVerbParameters::get (id)));
                }

                view = std::make_unique<CVerbView> (*this);
                addAndMakeVisible (*view);

                setResizable (true, true);
                setResizeLimits (CVerbView::minimumWidth, CVerbView::minimumHeight, 2400, 1600);
                setSize (CVerbView::defaultWidth, CVerbView::defaultHeight);
            }

            ~CVerbEditor() override
            {
                view.reset();
                setLookAndFeel (nullptr);
            }

            void resized() override                                                 { view->setBounds (getLocalBounds()); }

            std::shared_ptr<KnobParameter> parameter (CVerbParameters::ID id) override  { return parameters[(size_t) id]; }
            ReverbEngine& getEngine() override                                      { return processor.engine; }

        private:
            CVerbProcessor& processor;
            CDAWLookAndFeel lookAndFeel;
            std::vector<std::shared_ptr<KnobParameter>> parameters;
            std::unique_ptr<CVerbView> view;
        };
    }

    //==============================================================================
    CVerbProcessor::CVerbProcessor()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "CVerb", createLayout())
    {
        for (int i = 0; i < CVerbParameters::count; ++i)
        {
            auto id = juce::String (CVerbParameters::get ((CVerbParameters::ID) i).id);
            parameters[(size_t) i] = state.getParameter (id);
            values[(size_t) i] = state.getRawParameterValue (id);
            jassert (parameters[(size_t) i] != nullptr && values[(size_t) i] != nullptr);
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout CVerbProcessor::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (int i = 0; i < CVerbParameters::count; ++i)
        {
            auto& spec = CVerbParameters::get ((CVerbParameters::ID) i);
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction ([toText = spec.toText] (float v, int) { return toText (v); })
                                  .withValueFromStringFunction ([fromText = spec.fromText] (const juce::String& s) { return fromText (s); });

            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spec.id, 1 }, spec.name, spec.getRange(),
                                                                     spec.defaultValue, attributes));
        }

        return layout;
    }

    ReverbEngine::Settings CVerbProcessor::getSettings() const
    {
        return CVerbParameters::toSettings ([this] (CVerbParameters::ID id) { return values[(size_t) id]->load(); });
    }

    double CVerbProcessor::getTailLengthSeconds() const
    {
        auto s = getSettings();
        return s.preDelay * 0.001 + s.decay * juce::jmax (1.0f, s.lowDecay) * 1.1;
    }

    bool CVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        // Stereo out; mono or stereo in.
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    void CVerbProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
    {
        maxBlockSize = juce::jmax (1, maximumBlockSize);
        engine.prepare (sampleRate, maxBlockSize, getSettings());
    }

    void CVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        // Mono in, stereo out: the second channel starts as a copy of the first.
        if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

        const auto settings = getSettings();

        // In blocks no longer than the engine was prepared for (some hosts send more).
        for (int start = 0; start < buffer.getNumSamples(); start += maxBlockSize)
            engine.process (buffer, start, juce::jmin (maxBlockSize, buffer.getNumSamples() - start), settings);
    }

    juce::AudioProcessorEditor* CVerbProcessor::createEditor()
    {
        return new CVerbEditor (*this);
    }

    void CVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void CVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (state.state.getType()))
                state.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CVerbProcessor();
}
