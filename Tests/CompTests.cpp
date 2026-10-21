#include "TestEngine.h"
#include "Model/CompOps.h"

namespace cdaw::test
{
    struct CompTests : public juce::UnitTest
    {
        CompTests() : juce::UnitTest ("Takes and comping", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();

            // Three takes of the same 4 s riff at different levels (as loop recording would leave them).
            auto take1 = writeSineFile ("take1", 4.0, 0.1f);
            auto take2 = writeSineFile ("take2", 4.0, 0.3f);
            auto take3 = writeSineFile ("take3", 4.0, 0.6f);

            auto edit = te::test_utilities::createTestEdit (engine, 1);
            auto track = te::getAudioTracks (*edit)[0];
            auto clip = track->insertWaveClip ("riff", take1, { { te::TimePosition(), te::TimeDuration::fromSeconds (4.0) }, {} }, false);
            clip->addTake (take1);
            clip->addTake (take2);
            clip->addTake (take3);

            beginTest ("Take list and switching");
            {
                expectEquals (CompOps::getNumTakes (*clip), 3);
                expectEquals (CompOps::getActiveTake (*clip), 0);
                CompOps::setActiveTake (*clip, 1);
                expectEquals (CompOps::getActiveTake (*clip), 1);
                expectEquals (CompOps::getNumTakes (*clip), 3, "Switching must not delete takes");
                CompOps::setActiveTake (*clip, 0);
            }

            beginTest ("Comp the middle of the clip to take 3");
            {
                CompOps::compRange (*track, { te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (3.0) }, 2);
                auto clips = track->getClips();
                expectEquals (clips.size(), 3);

                auto out = te::test_utilities::renderToAudioBuffer (*edit);
                auto peakIn = [&] (double a, double b)
                {
                    auto s = (int) (a * out.sampleRate), e = (int) (b * out.sampleRate);
                    return out.buffer.getMagnitude (0, s, e - s);
                };

                expectWithinAbsoluteError (peakIn (0.2, 0.8), 0.1f, 0.02f);
                expectWithinAbsoluteError (peakIn (1.2, 2.8), 0.6f, 0.05f);
                expectWithinAbsoluteError (peakIn (3.2, 3.8), 0.1f, 0.02f);

                // Every piece still knows all its takes.
                for (auto c : track->getClips())
                    expectEquals (CompOps::getNumTakes (*c), 3);
            }

            beginTest ("Unpack takes to tracks");
            {
                auto pieces = track->getClips();
                if (auto wc = dynamic_cast<te::WaveAudioClip*> (pieces[0]))
                {
                    auto created = CompOps::unpackTakesToTracks (*wc);
                    expectEquals (created.size(), 3);
                    expectEquals (te::getAudioTracks (*edit).size(), 4);
                }
            }
        }
    };

    static CompTests compTests;
}
