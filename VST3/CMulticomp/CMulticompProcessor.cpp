#include "CMulticompProcessor.h"
#include "Shared/Multiband/MultibandView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"

namespace cdaw
{
    namespace
    {
        /** A host parameter for the shared knobs and views. */
        struct HostKnobParameter : public KnobParameter
        {
            HostKnobParameter (juce::RangedAudioParameter& p, const MultibandParameters::Spec& s) : param (p), spec (s) {}

            float getValue() const override                     { return param.convertFrom0to1 (param.getValue()); }
            void setValue (float v) override                    { param.setValueNotifyingHost (param.convertTo0to1 (v)); }
            void beginGesture() override                        { param.beginChangeGesture(); }
            void endGesture() override                          { param.endChangeGesture(); }
            juce::Range<float> getRange() const override        { return param.getNormalisableRange().getRange(); }
            juce::String getText (float v) const override       { return spec.toText (v); }

            juce::RangedAudioParameter& param;
            const MultibandParameters::Spec& spec;
        };

        /** The plugin window: the shared C-Multicomp view, in CDAW's look. */
        class CMulticompEditor : public juce::AudioProcessorEditor,
                                 public MultibandModel
        {
        public:
            explicit CMulticompEditor (CMulticompProcessor& p) : juce::AudioProcessorEditor (p), processor (p)
            {
                setLookAndFeel (&lookAndFeel);

                for (int i = 0; i < MultibandParameters::count; ++i)
                    parameters.push_back (std::make_shared<HostKnobParameter> (processor.getHostParameter (i), MultibandParameters::get (i)));

                view = std::make_unique<MultibandView> (*this);
                addAndMakeVisible (*view);

                setResizable (true, true);
                setResizeLimits (MultibandView::minimumWidth, MultibandView::minimumHeight, 2400, 1600);
                setSize (MultibandView::defaultWidth, MultibandView::defaultHeight);
            }

            ~CMulticompEditor() override
            {
                view.reset();
                setLookAndFeel (nullptr);
            }

            void resized() override                                             { view->setBounds (getLocalBounds()); }

            std::shared_ptr<KnobParameter> parameter (int index) override       { return parameters[(size_t) index]; }
            MultibandEngine& getEngine() override                               { return processor.engine; }

        private:
            CMulticompProcessor& processor;
            CDAWLookAndFeel lookAndFeel;
            juce::TooltipWindow tooltipWindow { this };
            std::vector<std::shared_ptr<KnobParameter>> parameters;
            std::unique_ptr<MultibandView> view;
        };
    }

    //==============================================================================
    CMulticompProcessor::CMulticompProcessor()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "CMulticomp", createLayout())
    {
        for (int i = 0; i < MultibandParameters::count; ++i)
        {
            auto id = MultibandParameters::get (i).id;
            parameters[(size_t) i] = state.getParameter (id);
            values[(size_t) i] = state.getRawParameterValue (id);
            jassert (parameters[(size_t) i] != nullptr && values[(size_t) i] != nullptr);
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout CMulticompProcessor::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (int i = 0; i < MultibandParameters::count; ++i)
        {
            auto& spec = MultibandParameters::get (i);
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction ([toText = spec.toText] (float v, int) { return toText (v); })
                                  .withValueFromStringFunction ([fromText = spec.fromText] (const juce::String& s) { return fromText (s); });

            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spec.id, 1 }, spec.name, spec.getRange(),
                                                                     spec.defaultValue, attributes));
        }

        return layout;
    }

    MultibandEngine::Settings CMulticompProcessor::getSettings() const
    {
        return MultibandParameters::toSettings ([this] (int i) { return values[(size_t) i]->load(); });
    }

    bool CMulticompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        // Mono or stereo, the same in and out.
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    void CMulticompProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
    {
        engine.prepare (sampleRate, maximumBlockSize);
    }

    void CMulticompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;
        engine.process (buffer, 0, buffer.getNumSamples(), getSettings());
    }

    juce::AudioProcessorEditor* CMulticompProcessor::createEditor()
    {
        return new CMulticompEditor (*this);
    }

    void CMulticompProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void CMulticompProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (state.state.getType()))
                state.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CMulticompProcessor();
}
