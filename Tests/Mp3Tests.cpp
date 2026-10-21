#include "TestEngine.h"
#include "Model/ClipOps.h"
#include "Model/ProjectSession.h"

namespace cdaw::test
{
    struct Mp3Tests : public juce::UnitTest
    {
        Mp3Tests() : juce::UnitTest ("MP3 import", "CDAW") {}

        void runTest() override
        {
            auto mp3 = juce::File (juce::SystemStats::getEnvironmentVariable ("CDAW_TEST_MP3", {}));
            if (! mp3.existsAsFile())
                return;

            beginTest ("Imported MP3 renders audio");
            auto& engine = getEngine();
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CDAW_Mp3Demo");
            dir.deleteRecursively(); dir.createDirectory();
            ProjectSession s (engine, ProjectSession::createProjectFolder (dir, "Mp3"));
            auto clips = ClipOps::importAudioFilesNow (s, { mp3 }, nullptr, te::TimePosition(), true);
            expectEquals (clips.size(), 1);

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (clips.getFirst()))
            {
                logMessage ("Clip file: " + wc->getAudioFile().getFile().getFullPathName()
                            + "  len " + juce::String (wc->getPosition().getLength().inSeconds()) + "s  ch "
                            + juce::String (wc->getAudioFile().getNumChannels()) + " rate " + juce::String (wc->getAudioFile().getSampleRate()));
            }

            s.save();
            auto out = te::test_utilities::renderToAudioBuffer (s.getEdit());
            auto peak = out.buffer.getMagnitude (0, out.buffer.getNumSamples());
            logMessage ("Render peak " + juce::String (peak, 4) + ", samples " + juce::String (out.buffer.getNumSamples()));
            expect (peak > 0.001f, "MP3 clip renders silence");
        }
    };
    static Mp3Tests mp3Tests;
}
