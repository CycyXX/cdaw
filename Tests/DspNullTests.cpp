// Guards optimisations that must not change the sound. With CDAW_DSP_GOLDEN_DIR set, the first run
// writes every engine's render to that folder; later runs compare against them and fail if any
// differs by more than -120 dB. A golden set only means something from one build configuration.

#include "TestEngine.h"
#include "DspScenarios.h"

namespace cdaw::test
{
    struct DspNullTests : public juce::UnitTest
    {
        DspNullTests() : juce::UnitTest ("DSP null test", "CDAW") {}

        static constexpr float limit = 1.0e-6f;     // -120 dB

        void runTest() override
        {
            auto folder = juce::SystemStats::getEnvironmentVariable ("CDAW_DSP_GOLDEN_DIR", {});

            if (folder.isEmpty())
                return;

            juce::File dir (folder);
            dir.createDirectory();

            for (auto& scenario : dsp::allScenarios())
            {
                beginTest (scenario.name);
                check (scenario.name, dsp::render (scenario), dir);

                // Engines that rest on silence must pick up exactly as if they hadn't. (The settings
                // change during the gap, too.)
                const bool rests = scenario.name.startsWith ("dist_") || scenario.name.startsWith ("multiband_")
                                    || scenario.name.startsWith ("chorus_") || scenario.name == "synth_default"
                                    || scenario.name == "lead_default" || scenario.name == "lead_dist";

                if (rests)
                    check (scenario.name + "_gap", dsp::render (scenario, 14.0, 8.0), dir);
            }
        }

        void check (const juce::String& name, const juce::AudioBuffer<float>& output, const juce::File& dir)
        {
            checkHealthy (name, output);
            auto file = dir.getChildFile (name + ".f32");

            if (file.existsAsFile())
                compare (name, output, file);
            else
                write (output, file);
        }

        void checkHealthy (const juce::String& name, const juce::AudioBuffer<float>& output)
        {
            float peak = 0.0f;
            bool finite = true;

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                for (int i = 0; i < output.getNumSamples(); ++i)
                {
                    auto v = output.getSample (ch, i);
                    finite = finite && std::isfinite (v);
                    peak = juce::jmax (peak, std::abs (v));
                }

            expect (finite, name + " produced a non-finite sample");
            expect (peak > 1.0e-3f, name + " is silent: it tests nothing");
        }

        static void write (const juce::AudioBuffer<float>& output, const juce::File& file)
        {
            file.deleteFile();
            juce::FileOutputStream out (file);

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                out.write (output.getReadPointer (ch), sizeof (float) * (size_t) output.getNumSamples());
        }

        void compare (const juce::String& name, const juce::AudioBuffer<float>& output, const juce::File& file)
        {
            juce::MemoryBlock golden;
            expect (file.loadFileAsData (golden), name + ": can't read " + file.getFullPathName());

            const auto expected = sizeof (float) * (size_t) output.getNumSamples() * (size_t) output.getNumChannels();
            expect (golden.getSize() == expected, name + ": wrong length");

            if (golden.getSize() != expected)
                return;

            auto* reference = static_cast<const float*> (golden.getData());
            float worst = 0.0f;
            int firstDifference = -1, worstAt = -1;

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
            {
                auto* now = output.getReadPointer (ch);
                auto* then = reference + ch * output.getNumSamples();

                for (int i = 0; i < output.getNumSamples(); ++i)
                {
                    const auto difference = std::abs (now[i] - then[i]);

                    if (difference > 0.0f && firstDifference < 0)
                        firstDifference = i;

                    if (difference > worst)
                    {
                        worst = difference;
                        worstAt = i;
                    }
                }
            }

            if (worst > 0.0f)
                logMessage (name + ": differs by " + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1)
                            + " dB, first at sample " + juce::String (firstDifference) + ", most at " + juce::String (worstAt));

            expect (worst <= limit, name + " changed by " + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dB");
        }
    };

    static DspNullTests dspNullTests;
}
