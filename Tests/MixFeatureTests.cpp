#include "TestEngine.h"
#include "Engine/MasterAnalyser.h"
#include "Model/ClipOps.h"
#include "Model/MixerOps.h"

namespace cdaw::test
{
    struct MixFeatureTests : public juce::UnitTest
    {
        MixFeatureTests() : juce::UnitTest ("Sidechain, analyser, crossfades, loops", "CDAW") {}

        static te::TimeRange secs (double a, double b)
        {
            return { te::TimePosition::fromSeconds (a), te::TimePosition::fromSeconds (b) };
        }

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("A sidechained compressor ducks only while the key track plays");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 2);
                auto kick = te::getAudioTracks (*edit)[0];
                auto bass = te::getAudioTracks (*edit)[1];
                kick->setName ("Kick");

                // Loud key for the first second only; quiet steady bass for two seconds.
                kick->insertWaveClip ("k", writeSineFile ("sc_key", 1.0, 0.8f, 60.0), { secs (0.0, 1.0), {} }, false);
                bass->insertWaveClip ("b", writeSineFile ("sc_bass", 2.0, 0.1f, 110.0), { secs (0.0, 2.0), {} }, false);

                auto plugin = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
                bass->pluginList.insertPlugin (plugin, 0, nullptr);
                auto comp = dynamic_cast<te::CompressorPlugin*> (plugin.get());
                expect (comp != nullptr);
                expect (MixerOps::canSidechain (*comp), "Compressor should offer a sidechain");

                comp->thresholdGain.setParameter (0.2f, juce::sendNotification);
                comp->ratio.setParameter (0.05f, juce::sendNotification);
                comp->attackMs.setParameter (1.0f, juce::sendNotification);
                comp->releaseMs.setParameter (30.0f, juce::sendNotification);

                expect (! MixerOps::getSidechainSources (*comp).contains (bass), "A track can't key itself");

                // The kick is in both renders, so the difference between them is only what the
                // sidechain did to the bass: removed while the kick plays, nothing after it.
                auto without = te::test_utilities::renderToAudioBuffer (*edit);

                MixerOps::setSidechainSource (*comp, kick);
                expect (MixerOps::getSidechainSource (*comp) == kick);
                expect (comp->useSidechainTrigger.get());

                auto with = te::test_utilities::renderToAudioBuffer (*edit);
                juce::AudioBuffer<float> diff (without.buffer);
                diff.addFrom (0, 0, with.buffer, 0, 0, juce::jmin (diff.getNumSamples(), with.buffer.getNumSamples()), -1.0f);

                auto rmsBetween = [&] (double a, double b)
                {
                    auto s = (int) (a * with.sampleRate), e = (int) (b * with.sampleRate);
                    return diff.getRMSLevel (0, s, juce::jmin (e, diff.getNumSamples()) - s);
                };

                auto duckedAmount = rmsBetween (0.3, 0.9);
                auto afterKick = rmsBetween (1.4, 1.9);
                logMessage ("Bass removed by the sidechain while keyed: RMS " + juce::String (duckedAmount, 4)
                            + " (bass alone is 0.0707), after the kick: " + juce::String (afterKick, 4));
                expectGreaterThan (duckedAmount, 0.04f);
                expectLessThan (afterKick, 0.01f);

                MixerOps::setSidechainSource (*comp, nullptr);
                expect (MixerOps::getSidechainSource (*comp) == nullptr);
                expect (! comp->useSidechainTrigger.get());
            }

            beginTest ("Analyser FIFO passes audio only while active");
            {
                AnalyserFifo fifo;
                juce::AudioBuffer<float> buffer (2, 256);
                buffer.clear();
                buffer.setSample (0, 10, 1.0f);
                buffer.setSample (1, 10, 0.5f);

                fifo.push (buffer);
                std::vector<float> out (1024);
                expectEquals (fifo.pull (out.data(), 1024), 0);

                fifo.active = true;
                fifo.push (buffer);
                expectEquals (fifo.pull (out.data(), 1024), 256);
                expectWithinAbsoluteError (out[10], 0.75f, 1e-6f);   // mono sum of both channels
            }

            beginTest ("Crossfading touching clips overlaps them with equal-power fades");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto whole = track->insertWaveClip ("w", writeSineFile ("xf_src", 4.0, 0.5f), { secs (0.0, 4.0), {} }, false);
                auto right = dynamic_cast<te::AudioClipBase*> (track->splitClip (*whole, te::TimePosition::fromSeconds (2.0)));
                auto left = dynamic_cast<te::AudioClipBase*> (whole.get());
                expect (left != nullptr && right != nullptr);

                auto made = ClipOps::crossfadeClips ({ left, right }, te::TimeDuration::fromSeconds (0.02));
                expectEquals (made, 1);
                expectWithinAbsoluteError (left->getPosition().getEnd().inSeconds(), 2.01, 1e-4);
                expectWithinAbsoluteError (right->getPosition().getStart().inSeconds(), 1.99, 1e-4);
                expectWithinAbsoluteError (left->getFadeOut().inSeconds(), 0.02, 1e-4);
                expectWithinAbsoluteError (right->getFadeIn().inSeconds(), 0.02, 1e-4);
                expect (left->getFadeOutType() == te::AudioFadeCurve::convex);

                // The right clip's audio stays in sync (its start moved earlier, the content didn't).
                expectWithinAbsoluteError (right->getPosition().getOffset().inSeconds(), 1.99, 1e-4);

                // No dropout across the join.
                auto out = te::test_utilities::renderToAudioBuffer (*edit);
                auto join = te::test_utilities::getRMSLevel (out, secs (1.995, 2.005), 0);
                auto body = te::test_utilities::getRMSLevel (out, secs (1.0, 1.5), 0);
                expectGreaterThan (join, body * 0.7f);
            }

            beginTest ("Clips with a gap aren't crossfaded");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto file = writeSineFile ("xf_gap", 2.0, 0.5f);
                auto a = track->insertWaveClip ("a", file, { secs (0.0, 1.0), {} }, false);
                auto b = track->insertWaveClip ("b", file, { secs (1.5, 2.5), {} }, false);
                expectEquals (ClipOps::crossfadeClips ({ a.get(), b.get() }, te::TimeDuration::fromSeconds (0.02)), 0);
            }

            beginTest ("A looped MIDI clip repeats its content when extended");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto& ts = edit->tempoSequence;
                auto oneBar = ts.toTime (te::tempo::BarsAndBeats { 1, {} });
                auto mc = track->insertMIDIClip ({ te::TimePosition(), oneBar }, nullptr);
                mc->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

                ClipOps::setLooping (*mc, true);
                expect (mc->isLooping());

                mc->setEnd (ts.toTime (te::tempo::BarsAndBeats { 4, {} }), true);
                expectEquals (mc->getSequenceLooped().getNotes().size(), 4);

                ClipOps::setLooping (*mc, false);
                expect (! mc->isLooping());
            }
        }
    };

    static MixFeatureTests mixFeatureTests;
}
