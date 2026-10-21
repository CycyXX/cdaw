#include "EqPlugin.h"
#include "../Engine/MasterAnalyser.h"
#include "../UI/Plugins/EqEditor.h"

namespace cdaw
{
    const char* EqPlugin::xmlTypeName = "cdawEq";

    namespace
    {
        juce::String formatHz (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                                 : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        float parseHz (const juce::String& s)
        {
            auto v = s.getFloatValue();
            return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
        }
    }

    bool EqPlugin::Settings::operator!= (const Settings& o) const
    {
        return on != o.on || type != o.type || slope != o.slope || freq != o.freq || gain != o.gain || q != o.q;
    }

    EqMath::BandType EqPlugin::getDefaultType (int band)
    {
        using T = EqMath::BandType;
        static constexpr T types[numBands] { T::lowCut, T::lowShelf, T::peak, T::peak, T::peak, T::peak, T::highShelf, T::highCut };
        return types[juce::jlimit (0, numBands - 1, band)];
    }

    float EqPlugin::getDefaultFreq (int band)
    {
        static constexpr float freqs[numBands] { 40.0f, 100.0f, 250.0f, 600.0f, 1500.0f, 3500.0f, 8000.0f, 16000.0f };
        return freqs[juce::jlimit (0, numBands - 1, band)];
    }

    float EqPlugin::getDefaultQ (EqMath::BandType t)
    {
        return t == EqMath::BandType::peak ? 1.0f : t == EqMath::BandType::notch ? 4.0f : 0.71f;
    }

    EqPlugin::EqPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        auto* um = getUndoManager();

        for (int i = 0; i < numBands; ++i)
        {
            auto& b = bands[(size_t) i];
            auto prefix = "b" + juce::String (i + 1);
            auto name = TRANS("Band") + " " + juce::String (i + 1) + " ";
            auto type = getDefaultType (i);

            juce::NormalisableRange<float> freqRange (minFreq, maxFreq);
            freqRange.setSkewForCentre (1000.0f);
            juce::NormalisableRange<float> qRange (minQ, maxQ);
            qRange.setSkewForCentre (1.0f);

            b.on   = { *this, prefix + "on", juce::Identifier (prefix + "on"), name + TRANS("On"), 0.0f, { 0.0f, 1.0f, 1.0f },
                       [] (float v) { return v >= 0.5f ? juce::String ("On") : juce::String ("Off"); },
                       [] (const juce::String& s) { return s.trim().equalsIgnoreCase ("on") ? 1.0f : 0.0f; } };
            b.freq = { *this, prefix + "freq", juce::Identifier (prefix + "freq"), name + TRANS("Frequency"), getDefaultFreq (i), freqRange,
                       formatHz, parseHz };
            b.gain = { *this, prefix + "gain", juce::Identifier (prefix + "gain"), name + TRANS("Gain"), 0.0f, { -maxGain, maxGain },
                       [] (float v) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; },
                       [] (const juce::String& s) { return s.getFloatValue(); } };
            b.q    = { *this, prefix + "q", juce::Identifier (prefix + "q"), name + "Q", getDefaultQ (type), qRange,
                       [] (float v) { return juce::String (v, v < 10.0f ? 2 : 1); },
                       [] (const juce::String& s) { return s.getFloatValue(); } };

            b.type.referTo (state, juce::Identifier (prefix + "type"), um, (int) type);
            b.slope.referTo (state, juce::Identifier (prefix + "slope"), um, 2);    // 24 dB/oct
        }

        output = { *this, "output", "output", TRANS("Output"), 0.0f, { -24.0f, 24.0f },
                   [] (float v) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; },
                   [] (const juce::String& s) { return s.getFloatValue(); } };

        preFifo = std::make_shared<AnalyserFifo>();
        postFifo = std::make_shared<AnalyserFifo>();
        updateTypeCache();
    }

    EqPlugin::~EqPlugin()
    {
        notifyListenersOfDeletion();

        for (auto& b : bands)
        {
            b.on.reset();
            b.freq.reset();
            b.gain.reset();
            b.q.reset();
        }

        output.reset();
    }

    std::unique_ptr<te::Plugin::EditorComponent> EqPlugin::createEditor()
    {
        return std::make_unique<EqEditor> (*this);
    }

    //==============================================================================
    EqMath::BandType EqPlugin::getType (int band) const
    {
        return (EqMath::BandType) juce::jlimit (0, (int) EqMath::BandType::numTypes - 1, typeCache[(size_t) band].load());
    }

    int EqPlugin::getSlope (int band) const
    {
        return juce::jlimit (0, (int) EqMath::slopes.size() - 1, slopeCache[(size_t) band].load());
    }

    bool EqPlugin::isOn (int band) const
    {
        return bands[(size_t) band].on.getCurrentValue() >= 0.5f;
    }

    void EqPlugin::setType (int band, EqMath::BandType t)       { bands[(size_t) band].type = (int) t; }
    void EqPlugin::setSlope (int band, int s)                   { bands[(size_t) band].slope = juce::jlimit (0, 3, s); }
    void EqPlugin::setOn (int band, bool shouldBeOn)            { bands[(size_t) band].on.setParameter (shouldBeOn ? 1.0f : 0.0f, juce::sendNotification); }

    void EqPlugin::updateTypeCache()
    {
        for (int i = 0; i < numBands; ++i)
        {
            typeCache[(size_t) i] = bands[(size_t) i].type.get();
            slopeCache[(size_t) i] = bands[(size_t) i].slope.get();
        }
    }

    void EqPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
    {
        if (v == state && (id.toString().endsWith ("type") || id.toString().endsWith ("slope")))
        {
            for (auto& b : bands)
            {
                b.type.forceUpdateOfCachedValue();
                b.slope.forceUpdateOfCachedValue();
            }

            updateTypeCache();
            changed();
        }

        te::Plugin::valueTreePropertyChanged (v, id);
    }

    void EqPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (auto& b : bands)
        {
            b.on.setFromValueTree (v);
            b.freq.setFromValueTree (v);
            b.gain.setFromValueTree (v);
            b.q.setFromValueTree (v);
            te::copyPropertiesToCachedValues (v, b.type, b.slope);
        }

        output.setFromValueTree (v);
        updateTypeCache();
    }

    EqMath::BandFilter EqPlugin::designBand (int band, double rate) const
    {
        auto& b = bands[(size_t) band];
        return EqMath::design (getType (band), getSlope (band), b.freq.getCurrentValue(), b.gain.getCurrentValue(),
                               b.q.getCurrentValue(), rate);
    }

    double EqPlugin::getResponseDb (double hz, double rate) const
    {
        double db = output.getCurrentValue();

        for (int i = 0; i < numBands; ++i)
            if (isOn (i))
                db += designBand (i, rate).magnitudeDb (hz, rate);

        return db;
    }

    //==============================================================================
    void EqPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        preFifo->sampleRate = info.sampleRate;
        postFifo->sampleRate = info.sampleRate;
        designedRate = 0.0;
        reset();
    }

    void EqPlugin::deinitialise() {}

    void EqPlugin::reset()
    {
        for (auto& band : filterState)
            for (auto& section : band)
                for (auto& s : section)
                    s = {};
    }

    void EqPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
            return;

        auto& buffer = *fc.destBuffer;
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        const int n = fc.bufferNumSamples, start = fc.bufferStartSample;

        if (numChannels == 0)
            return;

        // A view of just this block's main channels (no allocation for <= 32 channels).
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, start, n);
        preFifo->push (view);

        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const bool rateChanged = rate != designedRate;
        designedRate = rate;

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[(size_t) b];
            Settings now { band.on.getCurrentValue() >= 0.5f ? 1 : 0, typeCache[(size_t) b].load(), slopeCache[(size_t) b].load(),
                           band.freq.getCurrentValue(), band.gain.getCurrentValue(), band.q.getCurrentValue() };

            if (now != designed[(size_t) b] || rateChanged)
            {
                const bool switchedOn = now.on && ! designed[(size_t) b].on;
                const bool reshaped = now.type != designed[(size_t) b].type || now.slope != designed[(size_t) b].slope;

                filters[(size_t) b] = EqMath::design ((EqMath::BandType) juce::jlimit (0, 5, now.type), juce::jlimit (0, 3, now.slope),
                                                      now.freq, now.gain, now.q, rate);

                // A band coming in (or changing shape) starts from silence rather than stale state.
                if (switchedOn || reshaped)
                    for (auto& section : filterState[(size_t) b])
                        for (auto& s : section)
                            s = {};

                designed[(size_t) b] = now;
            }

            if (! now.on)
                continue;

            auto& f = filters[(size_t) b];

            for (int sec = 0; sec < f.numSections; ++sec)
            {
                auto& c = f.sections[(size_t) sec];

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& s = filterState[(size_t) b][(size_t) sec][(size_t) ch];
                    auto* data = buffer.getWritePointer (ch, start);
                    double z1 = s.z1, z2 = s.z2;

                    for (int i = 0; i < n; ++i)
                    {
                        const double x = data[i];
                        const double y = c.b0 * x + z1;
                        z1 = c.b1 * x - c.a1 * y + z2;
                        z2 = c.b2 * x - c.a2 * y;
                        data[i] = (float) y;
                    }

                    // Keep the filter out of denormals during silence.
                    s.z1 = std::abs (z1) < 1.0e-20 ? 0.0 : z1;
                    s.z2 = std::abs (z2) < 1.0e-20 ? 0.0 : z2;
                }
            }
        }

        const float outGain = juce::Decibels::decibelsToGain (output.getCurrentValue());
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGainRamp (ch, start, n, lastOutputGain, outGain);
        lastOutputGain = outGain;

        postFifo->push (view);
    }
}
