#pragma once

#include "PCH.h"
#include "Plugins/BuiltInPlugins.h"
#include <future>
#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

namespace cdaw::test
{
    /** Engine for tests: separate settings folder ("CDAW_Tests"), no audio device. */
    struct TestEngineBehaviour : public te::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override     { return false; }
    };

    struct TestUIBehaviour : public te::UIBehaviour
    {
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& job) override
        {
            while (job.runJob() != juce::ThreadPoolJob::jobHasFinished) {}
        }
    };

    /** Created on first use, destroyed by destroyEngine() before JUCE shuts down. */
    inline std::unique_ptr<te::Engine>& engineHolder()
    {
        static std::unique_ptr<te::Engine> engine;
        return engine;
    }

    inline te::Engine& getEngine()
    {
        auto& e = engineHolder();

        if (e == nullptr)
        {
            e = std::make_unique<te::Engine> (std::make_unique<te::PropertyStorage> ("CDAW_Tests"),
                                              std::make_unique<TestUIBehaviour>(),
                                              std::make_unique<TestEngineBehaviour>());

            BuiltInPlugins::registerTypes (*e);
        }

        return *e;
    }

    inline void destroyEngine()     { engineHolder().reset(); }

    inline juce::File getTestDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CDAW_Tests");
        d.createDirectory();
        return d;
    }

    /** Writes a constant-amplitude sine to a WAV file. */
    inline juce::File writeSineFile (const juce::String& name, double seconds, float amplitude,
                                     double freq = 1000.0, double sampleRate = 48000.0, int numChannels = 2)
    {
        auto file = getTestDir().getChildFile (name + ".wav");
        file.deleteFile();

        juce::AudioBuffer<float> buffer (numChannels, (int) (seconds * sampleRate));

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate));

        juce::WavAudioFormat wav;
        if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), sampleRate, (unsigned int) numChannels, 24, {}, 0)))
            {
                out.release();
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }

        return file;
    }
}
