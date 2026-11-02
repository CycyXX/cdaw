#include "CCompProcessor.h"
#include "Shared/Compressor/CompressorView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"

namespace cdaw
{
    namespace
    {
        /** A host parameter for the shared knobs and views. */
        struct HostKnobParameter : public KnobParameter
        {
            HostKnobParameter (juce::RangedAudioParameter& p, const CompressorParameters::Spec& s) : param (p), spec (s) {}

            float getValue() const override                     { return param.convertFrom0to1 (param.getValue()); }
            void setValue (float v) override                    { param.setValueNotifyingHost (param.convertTo0to1 (v)); }
            void beginGesture() override                        { param.beginChangeGesture(); }
            void endGesture() override                          { param.endChangeGesture(); }
            juce::Range<float> getRange() const override        { return param.getNormalisableRange().getRange(); }
            juce::String getText (float v) const override       { return spec.toText (v); }

            juce::RangedAudioParameter& param;
            const CompressorParameters::Spec& spec;
        };

        /** The plugin window: the shared C-Comp view, in CDAW's look. */
        class CCompEditor : public juce::AudioProcessorEditor,
                            public CompressorModel
        {
        public:
            explicit CCompEditor (CCompProcessor& p) : juce::AudioProcessorEditor (p), processor (p)
            {
                setLookAndFeel (&lookAndFeel);

                for (int i = 0; i < CompressorParameters::count; ++i)
                {
                    auto id = (CompressorParameters::ID) i;
                    parameters.push_back (std::make_shared<HostKnobParameter> (processor.getHostParameter (id), CompressorParameters::get (id)));
                }

                view = std::make_unique<CompressorView> (*this);
                addAndMakeVisible (*view);

                setResizable (true, true);
                setResizeLimits (CompressorView::minimumWidth, CompressorView::minimumHeight, 2400, 1600);
                setSize (CompressorView::defaultWidth, CompressorView::defaultHeight);
            }

            ~CCompEditor() override
            {
                view.reset();
                setLookAndFeel (nullptr);
            }

            void resized() override                                                 { view->setBounds (getLocalBounds()); }

            std::shared_ptr<KnobParameter> parameter (CompressorParameters::ID id) override  { return parameters[(size_t) id]; }
            CompressorEngine& getEngine() override                                  { return processor.engine; }

            // The host routes the sidechain input; the button switches the detector over to it.
            juce::String getSidechainSource() override
            {
                if (! processor.isSidechainOn())
                    return {};

                return processor.isSidechainConnected() ? "host input" : "host input (not routed)";
            }

            bool canChooseSidechain() const override                                { return true; }

            void showSidechainMenu (juce::Component&) override
            {
                auto& p = processor.getSidechainSwitch();
                p.beginChangeGesture();
                p.setValueNotifyingHost (processor.isSidechainOn() ? 0.0f : 1.0f);
                p.endChangeGesture();
            }

        private:
            CCompProcessor& processor;
            CDAWLookAndFeel lookAndFeel;
            juce::TooltipWindow tooltipWindow { this };
            std::vector<std::shared_ptr<KnobParameter>> parameters;
            std::unique_ptr<CompressorView> view;
        };
    }

    //==============================================================================
    CCompProcessor::CCompProcessor()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "CComp", createLayout())
    {
        for (int i = 0; i < CompressorParameters::count; ++i)
        {
            auto id = juce::String (CompressorParameters::get ((CompressorParameters::ID) i).id);
            parameters[(size_t) i] = state.getParameter (id);
            values[(size_t) i] = state.getRawParameterValue (id);
            jassert (parameters[(size_t) i] != nullptr && values[(size_t) i] != nullptr);
        }

        sidechainSwitch = state.getParameter (sidechainSwitchID);
        sidechainSwitchValue = state.getRawParameterValue (sidechainSwitchID);
        jassert (sidechainSwitch != nullptr && sidechainSwitchValue != nullptr);
    }

    bool CCompProcessor::isSidechainConnected() const
    {
        auto* bus = getBus (true, 1);
        return bus != nullptr && bus->isEnabled();
    }

    juce::AudioProcessorValueTreeState::ParameterLayout CCompProcessor::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (int i = 0; i < CompressorParameters::count; ++i)
        {
            auto& spec = CompressorParameters::get ((CompressorParameters::ID) i);
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction ([toText = spec.toText] (float v, int) { return toText (v); })
                                  .withValueFromStringFunction ([fromText = spec.fromText] (const juce::String& s) { return fromText (s); });

            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spec.id, 1 }, spec.name, spec.getRange(),
                                                                     spec.defaultValue, attributes));
        }

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { sidechainSwitchID, 1 }, "Sidechain", false));
        return layout;
    }

    CompressorEngine::Settings CCompProcessor::getSettings() const
    {
        return CompressorParameters::toSettings ([this] (CompressorParameters::ID id) { return values[(size_t) id]->load(); });
    }

    bool CCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        // Mono or stereo, the same in and out; the sidechain input is off, mono or stereo.
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        const auto sidechain = layouts.getChannelSet (true, 1);

        return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
               && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo());
    }

    void CCompProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
    {
        engine.prepare (sampleRate);
        key.assign ((size_t) juce::jmax (1, maximumBlockSize), 0.0f);
    }

    void CCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        auto main = getBusBuffer (buffer, true, 0);
        const bool keyed = isSidechainOn() && isSidechainConnected();
        juce::AudioBuffer<float> sidechain;
        if (keyed)
            sidechain = getBusBuffer (buffer, true, 1);

        const auto settings = getSettings();
        const int chunk = (int) key.size();

        // In blocks no longer than the key buffer (some hosts send more than they announced).
        for (int start = 0; start < main.getNumSamples(); start += chunk)
        {
            const int n = juce::jmin (chunk, main.getNumSamples() - start);

            if (keyed)
            {
                // The loudest of the sidechain channels, sample by sample.
                for (int i = 0; i < n; ++i)
                {
                    float loudest = 0.0f;
                    for (int ch = 0; ch < sidechain.getNumChannels(); ++ch)
                        loudest = juce::jmax (loudest, std::abs (sidechain.getSample (ch, start + i)));
                    key[(size_t) i] = loudest;
                }
            }

            engine.process (main, start, n, settings, keyed ? key.data() : nullptr);
        }
    }

    juce::AudioProcessorEditor* CCompProcessor::createEditor()
    {
        return new CCompEditor (*this);
    }

    void CCompProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void CCompProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (state.state.getType()))
                state.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CCompProcessor();
}
