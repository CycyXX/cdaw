#include "MultibandPlugin.h"
#include "../Engine/MasterAnalyser.h"
#include "../UI/Plugins/MultibandEditor.h"

namespace cdaw
{
    const char* MultibandPlugin::xmlTypeName = "cdawMultiband";

    namespace
    {
        juce::String formatHz (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        float parseHz (const juce::String& s)
        {
            auto v = s.getFloatValue();
            return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
        }

        juce::String formatDb (float v)     { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
        float parseFloat (const juce::String& s)    { return s.getFloatValue(); }
    }

    juce::String MultibandPlugin::getBandName (int band)
    {
        static const char* names[numBands] { "Low", "Low Mid", "High Mid", "High" };
        return names[juce::jlimit (0, numBands - 1, band)];
    }

    MultibandPlugin::MultibandPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        static constexpr float defaultCrossovers[numBands - 1] { 120.0f, 1000.0f, 6000.0f };
        static constexpr float defaultAttack[numBands] { 30.0f, 15.0f, 8.0f, 5.0f };
        static constexpr float defaultRelease[numBands] { 250.0f, 180.0f, 120.0f, 90.0f };

        for (int i = 0; i < numBands - 1; ++i)
        {
            juce::NormalisableRange<float> range (minCrossover, maxCrossover);
            range.setSkewForCentre (1000.0f);
            auto id = "x" + juce::String (i + 1);
            crossovers[(size_t) i] = { *this, id, juce::Identifier (id), TRANS("Crossover") + " " + juce::String (i + 1),
                                       defaultCrossovers[i], range, formatHz, parseHz };
        }

        for (int i = 0; i < numBands; ++i)
        {
            auto& b = bands[(size_t) i];
            auto p = "b" + juce::String (i + 1);
            auto name = getBandName (i) + " ";

            juce::NormalisableRange<float> ratioRange (1.0f, 20.0f);
            ratioRange.setSkewForCentre (4.0f);
            juce::NormalisableRange<float> attackRange (0.1f, 200.0f);
            attackRange.setSkewForCentre (15.0f);
            juce::NormalisableRange<float> releaseRange (5.0f, 1000.0f);
            releaseRange.setSkewForCentre (150.0f);

            // Bands start off: the plugin is transparent until a band is switched on.
            b.on        = { *this, p + "on", juce::Identifier (p + "on"), name + TRANS("Active"), 0.0f, { 0.0f, 1.0f, 1.0f },
                            [] (float v) { return v >= 0.5f ? juce::String ("On") : juce::String ("Bypassed"); }, parseFloat };
            b.solo      = { *this, p + "solo", juce::Identifier (p + "solo"), name + TRANS("Solo"), 0.0f, { 0.0f, 1.0f, 1.0f },
                            [] (float v) { return v >= 0.5f ? juce::String ("Solo") : juce::String ("-"); }, parseFloat };
            b.threshold = { *this, p + "threshold", juce::Identifier (p + "threshold"), name + TRANS("Threshold"), -18.0f, { -60.0f, 0.0f },
                            [] (float v) { return juce::String (v, 1) + " dB"; }, parseFloat };
            b.ratio     = { *this, p + "ratio", juce::Identifier (p + "ratio"), name + TRANS("Ratio"), 3.0f, ratioRange,
                            [] (float v) { return juce::String (v, 1) + ":1"; }, parseFloat };
            b.attack    = { *this, p + "attack", juce::Identifier (p + "attack"), name + TRANS("Attack"), defaultAttack[i], attackRange,
                            [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }, parseFloat };
            b.release   = { *this, p + "release", juce::Identifier (p + "release"), name + TRANS("Release"), defaultRelease[i], releaseRange,
                            [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, parseFloat };
            b.gain      = { *this, p + "gain", juce::Identifier (p + "gain"), name + TRANS("Gain"), 0.0f, { -24.0f, 24.0f }, formatDb, parseFloat };
        }

        output = { *this, "output", "output", TRANS("Output"), 0.0f, { -24.0f, 24.0f }, formatDb, parseFloat };

        preFifo = std::make_shared<AnalyserFifo>();
        postFifo = std::make_shared<AnalyserFifo>();
    }

    MultibandPlugin::~MultibandPlugin()
    {
        notifyListenersOfDeletion();

        for (auto& b : bands)
            for (auto* p : { &b.on, &b.solo, &b.threshold, &b.ratio, &b.attack, &b.release, &b.gain })
                p->reset();

        for (auto& x : crossovers)
            x.reset();

        output.reset();
    }

    std::unique_ptr<te::Plugin::EditorComponent> MultibandPlugin::createEditor()
    {
        return std::make_unique<MultibandEditor> (*this);
    }

    void MultibandPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (auto& b : bands)
            for (auto* p : { &b.on, &b.solo, &b.threshold, &b.ratio, &b.attack, &b.release, &b.gain })
                p->setFromValueTree (v);

        for (auto& x : crossovers)
            x.setFromValueTree (v);

        output.setFromValueTree (v);
    }

    //==============================================================================
    bool MultibandPlugin::anySoloed() const
    {
        for (int i = 0; i < numBands; ++i)
            if (isSoloed (i))
                return true;

        return false;
    }

    std::array<float, MultibandPlugin::numBands - 1> MultibandPlugin::getCrossovers() const
    {
        // A third of an octave apart at least, in order, whatever the parameters say.
        constexpr float minRatio = 1.26f;
        std::array<float, numBands - 1> f;

        for (int i = 0; i < numBands - 1; ++i)
            f[(size_t) i] = crossovers[(size_t) i].getCurrentValue();

        for (int i = 1; i < numBands - 1; ++i)
            f[(size_t) i] = juce::jmax (f[(size_t) i], f[(size_t) i - 1] * minRatio);

        return f;
    }

    juce::Range<float> MultibandPlugin::getBandRange (int band) const
    {
        auto f = getCrossovers();
        return { band == 0 ? 20.0f : f[(size_t) band - 1], band == numBands - 1 ? 20000.0f : f[(size_t) band] };
    }

    //==============================================================================
    void MultibandPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        preFifo->sampleRate = info.sampleRate;
        postFifo->sampleRate = info.sampleRate;

        for (auto& b : bandBuffers)
            b.setSize (2, juce::jmax (512, info.blockSizeSamples), false, true, false);

        designedRate = 0.0;
        reset();
    }

    void MultibandPlugin::deinitialise() {}

    void MultibandPlugin::reset()
    {
        for (auto& s : stages)
            for (auto* stage : { &s.lp1, &s.lp2, &s.hp1, &s.hp2, &s.ap[0], &s.ap[1] })
                stage->state = {};

        envelopeDb = {};
    }

    void MultibandPlugin::designCrossovers (double rate)
    {
        auto f = getCrossovers();

        if (rate == designedRate && f == designedFrequencies)
            return;

        for (int i = 0; i < numBands - 1; ++i)
        {
            auto x = EqMath::linkwitzRiley (f[(size_t) i], rate);
            auto& s = stages[(size_t) i];
            s.lp1.coeffs = s.lp2.coeffs = x.lowPass;
            s.hp1.coeffs = s.hp2.coeffs = x.highPass;
            s.ap[0].coeffs = s.ap[1].coeffs = x.allPass;
        }

        designedRate = rate;
        designedFrequencies = f;
    }

    double MultibandPlugin::run (Stage& stage, int ch, double x) noexcept
    {
        auto& c = stage.coeffs;
        auto& s = stage.state[(size_t) ch];
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void MultibandPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
            return;

        auto& buffer = *fc.destBuffer;
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        const int n = fc.bufferNumSamples, start = fc.bufferStartSample;

        if (numChannels == 0 || n > bandBuffers[0].getNumSamples())
            return;

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, start, n);
        preFifo->push (view);

        float inPeak[2] {};
        for (int ch = 0; ch < numChannels; ++ch)
            inPeak[ch] = view.getMagnitude (ch, 0, n);

        const double rate = getSampleRate();
        designCrossovers (rate);

        // Split: band 0 = LP0, then AP1, AP2;  band 1 = LP1 (HP0), then AP2;  band 2 = LP2 (HP1 (HP0));  band 3 = HP2 (HP1 (HP0)).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in = view.getReadPointer (ch);
            float* b0 = bandBuffers[0].getWritePointer (ch);
            float* b1 = bandBuffers[1].getWritePointer (ch);
            float* b2 = bandBuffers[2].getWritePointer (ch);
            float* b3 = bandBuffers[3].getWritePointer (ch);
            auto& x0 = stages[0];
            auto& x1 = stages[1];
            auto& x2 = stages[2];

            for (int i = 0; i < n; ++i)
            {
                const double x = in[i];
                double low = run (x0.lp2, ch, run (x0.lp1, ch, x));
                double rest = run (x0.hp2, ch, run (x0.hp1, ch, x));
                low = run (x2.ap[0], ch, run (x1.ap[0], ch, low));

                double lowMid = run (x1.lp2, ch, run (x1.lp1, ch, rest));
                rest = run (x1.hp2, ch, run (x1.hp1, ch, rest));
                lowMid = run (x2.ap[1], ch, lowMid);

                b0[i] = (float) low;
                b1[i] = (float) lowMid;
                b2[i] = (float) run (x2.lp2, ch, run (x2.lp1, ch, rest));
                b3[i] = (float) run (x2.hp2, ch, run (x2.hp1, ch, rest));
            }
        }

        // Compress each band (stereo-linked), then sum the audible ones.
        const bool solo = anySoloed();
        view.clear();

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[(size_t) b];
            auto& buf = bandBuffers[(size_t) b];
            float* l = buf.getWritePointer (0);
            float* r = numChannels > 1 ? buf.getWritePointer (1) : nullptr;

            const bool active = isOn (b);
            const float threshold = band.threshold.getCurrentValue();
            const float ratio = band.ratio.getCurrentValue();
            const double makeUp = active ? band.gain.getCurrentValue() : 0.0;
            const double attack = std::exp (-1.0 / (juce::jmax (0.05, (double) band.attack.getCurrentValue()) * 0.001 * rate));
            const double release = std::exp (-1.0 / (juce::jmax (1.0, (double) band.release.getCurrentValue()) * 0.001 * rate));
            double& env = envelopeDb[(size_t) b];
            float bandIn = 0.0f, bandOut = 0.0f;
            double most = 0.0;

            for (int i = 0; i < n; ++i)
            {
                const float detector = juce::jmax (std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.0f);
                bandIn = juce::jmax (bandIn, detector);

                double target = 0.0;
                if (active)
                {
                    const float inDb = detector > 1.0e-6f ? 20.0f * std::log10 (detector) : -120.0f;
                    target = transfer (inDb, threshold, ratio) - inDb;
                }

                env = target < env ? attack * env + (1.0 - attack) * target : release * env + (1.0 - release) * target;
                if (env > -1.0e-6)
                    env = 0.0;

                most = juce::jmin (most, env);
                const float g = (float) std::pow (10.0, (env + makeUp) / 20.0);
                l[i] *= g;
                if (r != nullptr) r[i] *= g;
                bandOut = juce::jmax (bandOut, juce::jmax (std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.0f));
            }

            band.inputPeak.push (bandIn);
            band.outputPeak.push (bandOut);
            band.reduction.push ((float) -most);

            if (! solo || isSoloed (b))
                for (int ch = 0; ch < numChannels; ++ch)
                    view.addFrom (ch, 0, buf, ch, 0, n);
        }

        const float outGain = juce::Decibels::decibelsToGain (output.getCurrentValue());
        for (int ch = 0; ch < numChannels; ++ch)
            view.applyGainRamp (ch, 0, n, lastOutputGain, outGain);
        lastOutputGain = outGain;

        for (int ch = 0; ch < 2; ++ch)
        {
            inputPeak[ch].push (inPeak[juce::jmin (ch, numChannels - 1)]);
            outputPeak[ch].push (view.getMagnitude (juce::jmin (ch, numChannels - 1), 0, n));
        }

        postFifo->push (view);
    }
}
