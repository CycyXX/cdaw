#include "TestEngine.h"
#include "Plugins/DistPlugin.h"
#include "Shared/Dist/DistPresets.h"
#include "Shared/Dist/DistView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Plugins/SharedEffectEditor.h"

namespace cdaw::test
{
    /** C-Dist: latency and blend, the curves as drawn, the voicings' harmonics, oversampling,
        gain compensation, the gate, the tone controls, the Bass crossover, extremes. */
    struct DistTests : public juce::UnitTest
    {
        DistTests() : juce::UnitTest ("C-Dist", "CDAW") {}

        using P = DistParameters::ID;
        using Settings = DistEngine::Settings;
        using Voicing = DistEngine::Voicing;
        static constexpr double rate = 48000.0;
        static constexpr double twoPi = juce::MathConstants<double>::twoPi;

        static Settings defaults()
        {
            return DistParameters::toSettings ([] (int id) { return DistParameters::get (id).defaultValue; });
        }

        /** Flat tone, no tight, no smooth: only the drive and the voicing's own filters. */
        static Settings flat (Voicing v, float drive)
        {
            auto s = defaults();
            s.voicing = v;
            s.drive = drive;
            s.tight = 20.0f;
            s.smooth = 20000.0f;
            return s;
        }

        static juce::AudioBuffer<float> run (DistEngine& engine, const Settings& s, const juce::AudioBuffer<float>& input, int blockSize = 512,
                                             std::function<void (int block)> each = {})
        {
            engine.prepare (rate, blockSize);
            juce::AudioBuffer<float> out (input);
            for (int start = 0, block = 0; start < out.getNumSamples(); start += blockSize, ++block)
            {
                engine.process (out, start, juce::jmin (blockSize, out.getNumSamples() - start), s);
                if (each)
                    each (block);
            }
            return out;
        }

        static juce::AudioBuffer<float> sine (double hz, float amplitude, double seconds, int channels = 2)
        {
            juce::AudioBuffer<float> b (channels, (int) (seconds * rate));
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int ch = 0; ch < channels; ++ch)
                    b.setSample (ch, i, amplitude * (float) std::sin (twoPi * hz * i / rate));
            return b;
        }

        /** A plucked note: a bright, decaying, harmonic-rich tone every half second, peaking near -12 dBFS. */
        static juce::AudioBuffer<float> plucks (double seconds, double hz = 110.0)
        {
            juce::AudioBuffer<float> b (2, (int) (seconds * rate));
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = std::fmod (i / rate, 0.5);
                double x = 0.0;
                for (int h = 1; h <= 12; ++h)
                    x += std::sin (twoPi * hz * h * t) / h * std::exp (-t * (2.0 + h * 0.8));
                const auto v = (float) (0.13 * x);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            return b;
        }

        static double toneDb (const juce::AudioBuffer<float>& b, double hz, double from, double to)
        {
            const int s = (int) (from * rate), n = (int) ((to - from) * rate);
            const double w = twoPi * hz / rate;
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double window = 0.5 - 0.5 * std::cos (twoPi * i / (n - 1));
                const double x = b.getSample (0, s + i) * window;
                re += x * std::cos (w * i);
                im -= x * std::sin (w * i);
            }
            return 20.0 * std::log10 (juce::jmax (1.0e-12, std::sqrt (re * re + im * im) / (n * 0.25)));
        }

        static float rmsDb (const juce::AudioBuffer<float>& b, double from, double to)
        {
            const int s = (int) (from * rate), e = juce::jmin (b.getNumSamples(), (int) (to * rate));
            return juce::Decibels::gainToDecibels (b.getRMSLevel (0, s, e - s), -200.0f);
        }

        /** Everything below `maxHz` that isn't a harmonic of f0, against the harmonics (dB). */
        static double offHarmonicDb (const juce::AudioBuffer<float>& b, double f0, double from, double maxHz)
        {
            constexpr int order = 14, size = 1 << order;
            juce::dsp::FFT fft (order);
            juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
            std::vector<float> data ((size_t) size * 2, 0.0f);
            for (int i = 0; i < size; ++i)
                data[(size_t) i] = b.getSample (0, (int) (from * rate) + i);
            window.multiplyWithWindowingTable (data.data(), (size_t) size);
            fft.performFrequencyOnlyForwardTransform (data.data());

            double harmonic = 0.0, other = 0.0;
            for (int k = 2; k < size / 2 && k * rate / size < maxHz; ++k)
            {
                const double hz = k * rate / size;
                const double nearest = std::round (hz / f0) * f0;
                const double power = (double) data[(size_t) k] * data[(size_t) k];
                (std::abs (hz - nearest) < 4.0 * rate / size ? harmonic : other) += power;
            }
            return 10.0 * std::log10 (other / harmonic);
        }

        /** Harmonics 2 to 10 against the fundamental (dB). */
        static double thdDb (const juce::AudioBuffer<float>& b, double f0, double from, double to)
        {
            double sum = 0.0;
            for (int h = 2; h <= 10; ++h)
                sum += std::pow (10.0, toneDb (b, f0 * h, from, to) / 10.0);
            return 10.0 * std::log10 (juce::jmax (1.0e-30, sum)) - toneDb (b, f0, from, to);
        }

        static bool finite (const juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (! std::isfinite (b.getSample (ch, i)))
                        return false;
            return true;
        }

        void runTest() override
        {
            beginTest ("Latency: the dry path is delayed to the oversampling's latency, and blending dry and wet doesn't comb");
            {
                DistEngine engine;
                auto s = flat (Voicing::boost, 0.0f);
                s.blend = 0.0f;
                juce::AudioBuffer<float> impulse (2, 4096);
                impulse.clear();
                impulse.setSample (0, 1000, 0.5f);
                impulse.setSample (1, 1000, 0.5f);
                auto dry = run (engine, s, impulse);
                int at = -1;
                for (int i = 0; i < dry.getNumSamples(); ++i)
                    if (std::abs (dry.getSample (0, i)) > 0.25f)
                        at = i;
                expectEquals (at - 1000, DistEngine::latencySamples, "the dry impulse comes out after the latency");

                // The wet path has the same latency: noise through it correlates best with the input
                // delayed by it (give or take the filters' own small delay).
                {
                    juce::Random random (3);
                    juce::AudioBuffer<float> noise (2, (int) (0.5 * rate));
                    for (int i = 0; i < noise.getNumSamples(); ++i)
                    {
                        const float v = 0.005f * (random.nextFloat() * 2.0f - 1.0f);
                        noise.setSample (0, i, v);
                        noise.setSample (1, i, v);
                    }
                    auto wet = run (engine, flat (Voicing::boost, 0.0f), noise);
                    int best = 0;
                    double bestSum = -1.0e30;
                    for (int lag = 0; lag < 80; ++lag)
                    {
                        double sum = 0.0;
                        for (int i = 2000; i < noise.getNumSamples() - 100; ++i)
                            sum += noise.getSample (0, i) * wet.getSample (0, i + lag);
                        if (sum > bestSum) { bestSum = sum; best = lag; }
                    }
                    logMessage ("The wet path's delay: " + juce::String (best) + " samples");
                    expect (best >= DistEngine::latencySamples && best <= DistEngine::latencySamples + 2, "wet delayed like dry: " + juce::String (best));
                }

                // Half dry, half wet: they add up (below the voicing's top end, where its low pass turns the phase).
                for (double hz : { 300.0, 1000.0, 3000.0, 6000.0 })
                {
                    auto wetOnly = flat (Voicing::boost, 0.0f), half = wetOnly;
                    half.blend = 0.5f;
                    auto in = sine (hz, 0.01f, 0.5);
                    const auto w = toneDb (run (engine, wetOnly, in), hz, 0.2, 0.45);
                    const auto m = toneDb (run (engine, half, in), hz, 0.2, 0.45);
                    const auto d = 20.0 * std::log10 (0.01);
                    const auto expected = 20.0 * std::log10 (0.5 * (std::pow (10.0, w / 20.0) + std::pow (10.0, d / 20.0)));
                    logMessage (juce::String (hz, 0) + " Hz: wet " + juce::String (w, 2) + " dB, dry " + juce::String (d, 2) + " dB, half and half "
                                + juce::String (m, 2) + " dB (in phase: " + juce::String (expected, 2) + ")");
                    expectWithinAbsoluteError (m, expected, hz > 5000.0 ? 1.0 : 0.5, "dry and wet in phase at " + juce::String (hz, 0) + " Hz");
                }
            }

            beginTest ("Boost at no drive is close to clean, at unity level");
            {
                DistEngine engine;
                auto out = run (engine, flat (Voicing::boost, 0.0f), sine (1000.0, 0.125f, 1.0));
                const auto level = toneDb (out, 1000.0, 0.3, 0.9) - 20.0 * std::log10 (0.125);
                const auto thd = thdDb (out, 1000.0, 0.3, 0.9);
                logMessage ("1 kHz at -18 dBFS through Boost at 0 %: " + juce::String (level, 2) + " dB, THD " + juce::String (thd, 1) + " dB");
                expectWithinAbsoluteError (level, 0.0, 1.5, "about unity: the compensation takes back its pre-emphasis");
                expectLessThan (thd, -35.0, "a touch of warmth, no more");
            }

            beginTest ("The engine's harmonics are the drawn curve's");
            {
                DistEngine engine;
                auto s = flat (Voicing::boost, 1.0f);
                auto out = run (engine, s, sine (100.0, 0.25f, 1.5));
                const auto level = engine.inputLevel.load();
                const auto drawn = DistEngine::harmonicsOf (s, 0.25f, level);
                for (int h : { 2, 3, 5, 7 })
                {
                    const auto measured = toneDb (out, 100.0 * h, 0.5, 1.4) - toneDb (out, 100.0, 0.5, 1.4);
                    logMessage ("Boost at 100 %, harmonic " + juce::String (h) + ": " + juce::String (measured, 2) + " dB (curve "
                                + juce::String (drawn[(size_t) h - 1], 2) + ")");
                    expectWithinAbsoluteError (measured, (double) drawn[(size_t) h - 1], h == 2 ? 2.0 : 0.5);
                }
            }

            beginTest ("Each voicing has its character");
            {
                for (int v = 0; v < DistEngine::numVoicings; ++v)
                {
                    auto s = flat ((Voicing) v, 0.6f);
                    const auto h = DistEngine::harmonicsOf (s, 0.25f);
                    logMessage (DistEngine::getVoicingNames()[v] + " at 60 %: 2nd " + juce::String (h[1], 1) + " dB, 3rd " + juce::String (h[2], 1)
                                + " dB, 5th " + juce::String (h[4], 1) + " dB, 9th " + juce::String (h[8], 1) + " dB");
                }

                auto harmonic = [] (Voicing v, int h) { return DistEngine::harmonicsOf (flat (v, 0.6f), 0.25f)[(size_t) h - 1]; };
                expectGreaterThan (harmonic (Voicing::crunch, 2), -30.0f, "Crunch is asymmetric: even harmonics");
                expectGreaterThan (harmonic (Voicing::fuzz, 2), -20.0f, "Fuzz even more so");
                expectGreaterThan (harmonic (Voicing::lead, 3), harmonic (Voicing::lead, 2) + 6.0f, "Lead is mostly odd");
                expectGreaterThan (harmonic (Voicing::modern, 3), harmonic (Voicing::modern, 2) + 6.0f, "Modern is mostly odd");
                expectGreaterThan (harmonic (Voicing::modern, 9), harmonic (Voicing::lead, 9), "Modern clips harder than Lead");
            }

            beginTest ("Oversampling keeps a high note at full drive clean");
            {
                DistEngine engine;
                // (Not at frequencies dividing the inner rate: their aliases would land on harmonics and hide.)
                for (auto [v, hz] : { std::pair (Voicing::modern, 1318.5), std::pair (Voicing::modern, 2937.0), std::pair (Voicing::fuzz, 2937.0),
                                      std::pair (Voicing::lead, 4937.0), std::pair (Voicing::crunch, 2937.0) })
                {
                    auto out = run (engine, flat (v, 1.0f), sine (hz, 0.25f, 1.0));
                    const auto db = offHarmonicDb (out, hz, 0.4, 20000.0);
                    logMessage (DistEngine::getVoicingNames()[(int) v] + " at 100 %, " + juce::String (hz, 0) + " Hz: off the harmonics below 20 kHz "
                                + juce::String (db, 1) + " dB");
                    expectLessThan (db, -60.0);
                }
            }

            beginTest ("Gain compensation: turning up the drive changes the sound, not the level");
            {
                DistEngine engine;
                const auto in = plucks (3.0);
                for (int v = 0; v < DistEngine::numVoicings; ++v)
                {
                    float lowest = 1000.0f, highest = -1000.0f;
                    juce::String levels;
                    for (float drive : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                    {
                        auto s = defaults();
                        s.voicing = (Voicing) v;
                        s.drive = drive;
                        const auto level = rmsDb (run (engine, s, in), 0.5, 3.0);
                        lowest = juce::jmin (lowest, level);
                        highest = juce::jmax (highest, level);
                        levels << juce::String (level, 1) << " ";
                    }
                    logMessage (DistEngine::getVoicingNames()[v] + ", drive 0 to 100 %: " + levels + "dB (input " + juce::String (rmsDb (in, 0.5, 3.0), 1) + ")");
                    expectLessThan (highest - lowest, 3.0f, DistEngine::getVoicingNames()[v] + " stays within 3 dB");
                    expectWithinAbsoluteError (0.5f * (highest + lowest), rmsDb (in, 0.5, 3.0) - 1.0f, 2.0f, "about the input's level");
                }
            }

            beginTest ("The gate: shut on the noise, open within a millisecond on a note, held open just under its threshold");
            {
                DistEngine engine;
                auto s = flat (Voicing::lead, 0.5f);
                s.gateOn = true;
                s.gateThresholdDb = -50.0f;
                s.gateRelease = 0.05f;

                juce::Random random (1);
                const int n = (int) (1.6 * rate);
                juce::AudioBuffer<float> in (2, n);
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / rate;
                    const float noise = 0.0003f * (random.nextFloat() * 2.0f - 1.0f);         // about -70 dBFS
                    const float amplitude = t >= 0.5 && t < 0.9 ? 0.1f : (t >= 0.9 && t < 1.2 ? 0.0025f : 0.0f);   // -20 dB, then -52 dB
                    const float v = noise + amplitude * (float) std::sin (twoPi * 440.0 * t);
                    in.setSample (0, i, v);
                    in.setSample (1, i, v);
                }

                auto out = run (engine, s, in);
                const double latency = DistEngine::latencySamples / rate;
                const auto steady = out.getMagnitude (0, (int) ((0.7 + latency) * rate), (int) (0.1 * rate));
                const auto early = out.getMagnitude (0, (int) ((0.5 + latency + 0.0012) * rate), (int) (0.004 * rate));
                logMessage ("Noise alone " + juce::String (rmsDb (out, 0.1, 0.45), 1) + " dB; 1.2 ms into the note " + juce::String (early / steady, 2)
                            + " of its level; just under the threshold " + juce::String (rmsDb (out, 1.0, 1.15), 1) + " dB; after " + juce::String (rmsDb (out, 1.35, 1.55), 1) + " dB");
                expectLessThan (rmsDb (out, 0.1, 0.45), -150.0f, "closed on the noise");
                expectGreaterThan (early / steady, 0.75f, "open at once");
                expectGreaterThan (rmsDb (out, 1.0, 1.15), -60.0f, "held open by the hysteresis");
                expectLessThan (rmsDb (out, 1.35, 1.55), -110.0f, "closed again");

                // No clicks: the gain moves at most a little per sample.
                auto ramp = s;
                ramp.voicing = Voicing::boost;
                ramp.drive = 0.0f;
                float largest = 0.0f;
                const auto stepped = run (engine, ramp, in, 512, {});
                for (int i = (int) ((0.5 + latency) * rate); i < (int) ((0.52 + latency) * rate); ++i)
                    largest = juce::jmax (largest, std::abs (stepped.getSample (0, i) - stepped.getSample (0, i - 1)));
                expectLessThan (largest, 0.1f * 0.12f, "the gate opens smoothly");
            }

            beginTest ("Tight, the voicing's filters and the tone controls measure as drawn");
            {
                DistEngine engine;
                auto s = flat (Voicing::lead, 0.0f);
                s.tight = 150.0f;
                s.bassDb = 6.0f;
                s.midDb = -4.0f;
                s.midHz = 1500.0f;
                s.trebleDb = 5.0f;
                s.smooth = 6000.0f;

                std::map<double, double> measured;
                for (double hz : { 60.0, 150.0, 400.0, 1000.0, 1500.0, 3000.0, 6000.0, 10000.0 })
                    measured[hz] = toneDb (run (engine, s, sine (hz, 0.001f, 0.6)), hz, 0.3, 0.55);

                const auto reference = DistEngine::getResponseDb (s, 1000.0, rate);
                for (auto [hz, db] : measured)
                {
                    const auto drawn = DistEngine::getResponseDb (s, hz, rate) - reference;
                    logMessage (juce::String (hz, 0) + " Hz: " + juce::String (db - measured[1000.0], 2) + " dB (drawn " + juce::String (drawn, 2) + ")");
                    expectWithinAbsoluteError (db - measured[1000.0], drawn, 0.5);
                }
            }

            beginTest ("Bass: lows under the crossover stay clean, highs are driven");
            {
                DistEngine engine;
                auto s = flat (Voicing::bass, 1.0f);
                s.tight = 250.0f;
                const auto low = thdDb (run (engine, s, sine (50.0, 0.25f, 1.5)), 50.0, 0.5, 1.4);
                const auto high = thdDb (run (engine, s, sine (1000.0, 0.25f, 1.0)), 1000.0, 0.3, 0.9);
                logMessage ("Bass at 100 %, crossover 250 Hz: 50 Hz THD " + juce::String (low, 1) + " dB, 1 kHz THD " + juce::String (high, 1) + " dB");
                expectLessThan (low, -40.0);
                expectGreaterThan (high, -20.0);
            }

            beginTest ("Fuzz: Character starves it, so quiet tails sputter out");
            {
                DistEngine engine;
                auto decaying = sine (220.0, 1.0f, 2.0);
                for (int i = 0; i < decaying.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        decaying.setSample (ch, i, decaying.getSample (ch, i) * 0.25f * (float) std::exp (-3.0 * i / rate));

                auto s = flat (Voicing::fuzz, 0.8f);
                s.character = 0.0f;
                const auto fed = rmsDb (run (engine, s, decaying), 1.5, 1.9);
                s.character = 1.0f;
                const auto starved = rmsDb (run (engine, s, decaying), 1.5, 1.9);
                logMessage ("The fuzz's tail (-40 dB in): " + juce::String (fed, 1) + " dB fed, " + juce::String (starved, 1) + " dB starved");
                expectLessThan (starved, fed - 10.0f);
            }

            beginTest ("Extremes stay finite and bounded; renders repeat whatever the block size");
            {
                juce::Random random (7);
                juce::AudioBuffer<float> noise (2, (int) (1.0 * rate));
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < noise.getNumSamples(); ++i)
                        noise.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                DistEngine engine;
                for (int v = 0; v < DistEngine::numVoicings; ++v)
                {
                    Settings s;
                    s.voicing = (Voicing) v;
                    s.drive = 1.0f;
                    s.character = 1.0f;
                    s.inputDb = 12.0f;
                    s.bassDb = s.midDb = s.trebleDb = 12.0f;
                    s.levelDb = 18.0f;
                    s.tight = 20.0f;
                    s.smooth = 20000.0f;
                    auto out = run (engine, s, noise);
                    expect (finite (out), DistEngine::getVoicingNames()[v] + " stays finite");
                    logMessage (DistEngine::getVoicingNames()[v] + " with everything up: peak " + juce::String (out.getMagnitude (0, out.getNumSamples()), 1));
                    expectLessThan (out.getMagnitude (0, out.getNumSamples()), 256.0f, DistEngine::getVoicingNames()[v] + ": loud as asked, no runaway");
                }

                auto s = defaults();
                s.voicing = Voicing::modern;
                s.gateOn = true;
                DistEngine a, b;
                const auto in = plucks (2.0);
                auto first = run (a, s, in, 512), second = run (b, s, in, 160);
                double signal = 0.0, difference = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < first.getNumSamples(); ++i)
                    {
                        signal += juce::square (first.getSample (ch, i));
                        difference += juce::square (first.getSample (ch, i) - second.getSample (ch, i));
                    }
                const auto db = 10.0 * std::log10 (juce::jmax (1.0e-30, difference) / signal);
                logMessage ("Modern in blocks of 512 and 160: difference " + juce::String (db, 1) + " dB");
                expectLessThan (db, -100.0);
            }

            beginTest ("Every factory preset loads, stays finite and plays at about the same loudness");
            {
                const auto in = plucks (3.0, 82.4);
                juce::StringArray names;
                for (auto& preset : DistPresets::factory())
                {
                    expect (! names.contains (preset.name), preset.name + " twice");
                    names.add (preset.name);
                    const auto values = DistPresets::valuesOf (preset);
                    auto s = DistParameters::toSettings ([&values] (int id) { return values[(size_t) id]; });
                    DistEngine engine;
                    auto out = run (engine, s, in);
                    const auto level = rmsDb (out, 0.5, 3.0);
                    logMessage (preset.category + " / " + preset.name + ": " + juce::String (level, 1) + " dB RMS, peak "
                                + juce::String (juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())), 1) + " dBFS");
                    expect (finite (out), preset.name + " stays finite");
                    expectWithinAbsoluteError (level, -25.0f, 1.5f, preset.name + " is level-matched");
                }

                expectEquals (DistPresets::factory()[0].name, juce::String ("Singing Lead"));
                expect (DistPresets::factory()[0].values.size() == 1, "the first preset is the defaults (only its voicing listed)");
                expectGreaterThan ((int) DistPresets::factory().size(), 18);
            }

            beginTest ("User presets save and load");
            {
                auto folder = getTestDir().getChildFile ("dist_presets");
                folder.deleteRecursively();
                DistPresets::setUserFolder (folder);

                auto values = DistPresets::valuesOf (DistPresets::factory()[5]);
                values[(size_t) P::drive] = 0.42f;
                expect (DistPresets::saveUser ("My Tone", [&values] (int id) { return values[(size_t) id]; }).wasOk());
                auto loaded = DistPresets::loadUser();
                expectEquals ((int) loaded.size(), 1);
                if (! loaded.empty())
                {
                    expectEquals (loaded[0].name, juce::String ("My Tone"));
                    expect (loaded[0].isUser());
                    auto back = DistPresets::valuesOf (loaded[0]);
                    for (int i = 0; i < P::count; ++i)
                        expectWithinAbsoluteError (back[(size_t) i], values[(size_t) i], 1.0e-4f, DistParameters::get (i).id);
                }
                expect (DistPresets::saveUser (" ", [&values] (int id) { return values[(size_t) id]; }).failed(), "a preset needs a name");
                DistPresets::setUserFolder ({});
            }

            beginTest ("C-Dist in Tracktion: distorts a track and reports its latency, so it stays in time");
            {
                // A sine, and later a click: through C-Dist, the click (dry, Blend 0) lands where it was.
                juce::AudioBuffer<float> source (2, (int) (2.0 * rate));
                source.clear();
                for (int i = 0; i < (int) (1.0 * rate); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        source.setSample (ch, i, 0.25f * (float) std::sin (twoPi * 220.0 * i / rate));
                const int click = (int) (1.5 * rate);
                source.setSample (0, click, 0.5f);
                source.setSample (1, click, 0.5f);
                auto wav = getTestDir().getChildFile ("dist_source.wav");
                wav.deleteFile();
                {
                    juce::WavAudioFormat format;
                    std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
                    if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (format.createWriterFor (stream.get(), rate, 2, 32, {}, 0)))
                    {
                        stream.release();
                        writer->writeFromAudioSampleBuffer (source, 0, source.getNumSamples());
                    }
                }

                auto render = [&] (float blendValue)
                {
                    auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                    auto track = te::getAudioTracks (*edit)[0];
                    track->insertWaveClip ("source", wav, { { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, {} }, false);
                    auto plugin = edit->getPluginCache().createNewPlugin (DistPlugin::xmlTypeName, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* dist = dynamic_cast<DistPlugin*> (plugin.get());
                    dist->parameter (P::voicing).setParameter ((float) Voicing::modern, juce::sendNotification);
                    dist->parameter (P::drive).setParameter (0.8f, juce::sendNotification);
                    dist->parameter (P::blend).setParameter (blendValue, juce::sendNotification);
                    expectWithinAbsoluteError (dist->getLatencySeconds(), DistEngine::latencySamples / 44100.0, 0.0005, "it reports the oversampling's latency");
                    return te::test_utilities::renderToAudioBuffer (*edit);
                };

                auto wet = render (1.0f);
                expect (finite (wet.buffer));
                const auto rendered = wet.buffer.getRMSLevel (0, (int) (0.3 * wet.sampleRate), (int) (0.5 * wet.sampleRate));
                logMessage ("Rendered at " + juce::String (wet.sampleRate) + " Hz: the sine through Modern at " + juce::String (juce::Decibels::gainToDecibels (rendered), 1) + " dB RMS");
                expectGreaterThan (rendered, 0.01f, "it plays");

                auto dry = render (0.0f);
                int at = -1;
                for (int i = (int) (1.2 * dry.sampleRate); i < dry.buffer.getNumSamples(); ++i)
                    if (std::abs (dry.buffer.getSample (0, i)) > 0.2f)
                    {
                        at = i;
                        break;
                    }
                const auto expected = (int) std::round (1.5 * dry.sampleRate);
                logMessage ("The click comes out at sample " + juce::String (at) + " (in at " + juce::String (expected) + ")");
                expectWithinAbsoluteError (at, expected, 2, "Tracktion lines up the reported latency");
            }

            beginTest ("The C-Dist VST3 sounds the same as the built-in C-Dist");
            checkVst3();

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Snapshot: C-Dist");
            {
                CDAWLookAndFeel laf;
                juce::LookAndFeel::setDefaultLookAndFeel (&laf);
                auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto plugin = edit->getPluginCache().createNewPlugin (DistPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* dist = dynamic_cast<DistPlugin*> (plugin.get());
                dist->parameter (P::gateOn).setParameter (1.0f, juce::sendNotification);
                dist->parameter (P::gateThreshold).setParameter (-58.0f, juce::sendNotification);
                dist->parameter (P::mid).setParameter (2.0f, juce::sendNotification);

                SharedEffectEditor<DistView, DistEngine> editor (*dist, dist->engine);
                editor.setSize (DistView::defaultWidth, DistView::defaultHeight);
                auto& view = editor.getView();

                // Plucked notes with a little hum between them, for the gate to shut on.
                auto in = plucks (3.0, 110.0);
                juce::Random random (5);
                for (int i = 0; i < in.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        in.setSample (ch, i, in.getSample (ch, i) * (std::fmod (i / rate, 1.0) < 0.62 ? 1.0f : 0.0f) + 0.0004f * (random.nextFloat() - 0.5f));

                dist->engine.prepare (rate, 512);
                const auto s = dist->getSettings();
                dist->engine.inputFifo->active = dist->engine.outputFifo->active = true;
                juce::AudioBuffer<float> block (2, 512);
                const int stop = (int) (2.45 * rate);
                for (int start = 0, n = 0; start + 512 <= stop; start += 512, ++n)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        block.copyFrom (ch, 0, in, ch, start, 512);
                    dist->engine.process (block, 0, 512, s);
                    if (n % 2 == 0)
                        view.tick();
                }

                auto dir = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {});
                auto folder = dir.isNotEmpty() ? juce::File (dir) : getTestDir().getChildFile ("snapshots");
                folder.createDirectory();
                auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
                auto file = folder.getChildFile ("c-dist.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
        }

        /** The VST3 from this build tree: the same audio as the built-in plugin (latency included),
            its editor, every parameter and its state. Skipped if not built. */
        void checkVst3()
        {
            auto& engine = getEngine();
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto config = exe.getParentDirectory();
            auto preset = config.getParentDirectory().getParentDirectory().getParentDirectory();
            auto bundle = preset.getChildFile ("VST3/CDist/CDist_artefacts").getChildFile (config.getFileName()).getChildFile ("VST3/C-Dist.vst3");

            juce::OwnedArray<juce::PluginDescription> types;
            if (bundle.exists())
                for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (types, bundle.getFullPathName());

            if (types.isEmpty())
            {
                logMessage ("C-Dist VST3 not built (" + bundle.getFullPathName() + "), skipping");
                return;
            }

            expectEquals (types[0]->name, juce::String ("C-Dist"));
            expect (! types[0]->isInstrument, "an effect");
            engine.getPluginManager().knownPluginList.addType (*types[0]);

            const auto& specs = DistParameters::all();
            const std::vector<std::pair<int, float>> changes { { P::voicing, (float) Voicing::fuzz }, { P::drive, 0.7f }, { P::tight, 200.0f },
                                                               { P::treble, 4.0f }, { P::blend, 0.7f }, { P::gateOn, 1.0f } };

            auto render = [&] (bool useVst3, bool change)
            {
                // The sine and click the Tracktion test wrote.
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                track->insertWaveClip ("source", getTestDir().getChildFile ("dist_source.wav"), { { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, {} }, false);

                if (useVst3)
                {
                    auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *types[0]);
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* instance = dynamic_cast<te::ExternalPlugin*> (plugin.get())->getAudioPluginInstance();
                    expect (instance != nullptr, "VST3 instance loads");
                    if (instance != nullptr && change)
                        for (auto& [id, value] : changes)
                            for (auto* param : instance->getParameters())
                                if (param->getName (64) == specs[(size_t) id].name)
                                    param->setValueNotifyingHost (specs[(size_t) id].getRange().convertTo0to1 (value));
                }
                else
                {
                    auto plugin = edit->getPluginCache().createNewPlugin (DistPlugin::xmlTypeName, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    if (change)
                        for (auto& [id, value] : changes)
                            dynamic_cast<DistPlugin*> (plugin.get())->parameter (id).setParameter (value, juce::sendNotification);
                }

                return te::test_utilities::renderToAudioBuffer (*edit);
            };

            auto compare = [&] (bool change)
            {
                auto vst3 = render (true, change), builtIn = render (false, change);
                const auto length = juce::jmin (vst3.buffer.getNumSamples(), builtIn.buffer.getNumSamples());
                double signal = 0.0, difference = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < length; ++i)
                    {
                        auto a = builtIn.buffer.getSample (ch, i), b = vst3.buffer.getSample (ch, i);
                        signal += a * a;
                        difference += (a - b) * (a - b);
                    }
                expectGreaterThan (signal, 1.0, "it plays");
                return 10.0 * std::log10 (juce::jmax (1.0e-30, difference) / juce::jmax (1.0e-30, signal));
            };

            auto defaults = compare (false), changed = compare (true);
            logMessage ("VST3 vs built-in C-Dist, difference below the signal: defaults " + juce::String (defaults, 1)
                        + " dB, changed settings " + juce::String (changed, 1) + " dB");
            expectLessThan (defaults, -80.0, "the same audio (and latency) with the same settings");
            expectLessThan (changed, -80.0, "settings sent through the host arrive with the first block");

            juce::String error;
            auto& formats = engine.getPluginManager().pluginFormatManager;
            auto first = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            auto second = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            expect (first != nullptr && second != nullptr, "instances: " + error);
            if (first == nullptr || second == nullptr)
                return;

            first->prepareToPlay (48000.0, 512);
            expectEquals (first->getLatencySamples(), DistEngine::latencySamples, "the VST3 reports its latency");

            if (auto* editor = first->createEditorIfNeeded())
            {
                expectEquals (editor->getWidth(), DistView::defaultWidth, "the VST3 editor opens at the view's size");
                delete editor;
            }
            else
            {
                expect (false, "the VST3 has an editor");
            }

            juce::StringArray hostNames;
            for (auto* param : first->getParameters())
                hostNames.add (param->getName (64));
            for (auto& spec : specs)
                expect (hostNames.contains (spec.name), juce::String (spec.name) + " is a host parameter");

            for (auto* param : first->getParameters())
                if (param->getName (64) == specs[P::drive].name)
                    param->setValueNotifyingHost (specs[P::drive].getRange().convertTo0to1 (0.37f));

            juce::MemoryBlock saved;
            first->getStateInformation (saved);
            second->setStateInformation (saved.getData(), (int) saved.getSize());
            for (auto* param : second->getParameters())
                if (param->getName (64) == specs[P::drive].name)
                    expectWithinAbsoluteError (specs[P::drive].getRange().convertFrom0to1 (param->getValue()), 0.37f, 0.001f, "settings restore");
        }
    };

    static DistTests distTests;
}
