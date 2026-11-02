// Clips keep their waveform's peaks between paints. Whatever has happened to the view, what's on
// screen must be exactly what a newly built view paints.

#include "TestEngine.h"
#include "Model/EditViewState.h"
#include "Model/ProjectSession.h"
#include "Model/TrackOps.h"
#include "UI/Arrange/ArrangeView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"

namespace cdaw::test
{
    struct ClipPaintTests : public juce::UnitTest
    {
        ClipPaintTests() : juce::UnitTest ("Clip painting", "CDAW") {}

        static void pump (int ms)   { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

        static int countDifferences (const juce::Image& first, const juce::Image& second, juce::Rectangle<int>& area)
        {
            int different = 0;
            const juce::Image::BitmapData a (first, juce::Image::BitmapData::readOnly), b (second, juce::Image::BitmapData::readOnly);

            for (int y = 0; y < first.getHeight(); ++y)
                for (int x = 0; x < first.getWidth(); ++x)
                    if (a.getPixelColour (x, y) != b.getPixelColour (x, y))
                    {
                        area = different++ == 0 ? juce::Rectangle<int> (x, y, 1, 1) : area.getUnion ({ x, y, 1, 1 });
                    }

            return different;
        }

        /** What it paints once its thumbnails have loaded. (A new thumbnail reports being fully
            loaded until it has been given its file, so that alone doesn't say it's finished.) */
        static juce::Image settledSnapshot (juce::Component& c, te::Engine& engine)
        {
            auto last = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            int unchanged = 0;

            // Tracktion reloads a clip's file 600 ms after a change to the clip: outlast that.
            for (int tries = 0; tries < 200 && unchanged < 8; ++tries)
            {
                pump (100);
                auto now = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
                juce::Rectangle<int> unused;

                unchanged = te::SmartThumbnail::areThumbnailsFullyLoaded (engine) && countDifferences (last, now, unused) == 0
                                ? unchanged + 1 : 0;
                last = now;
            }

            return last;
        }

        /** A file whose level changes all the way along, so a misplaced column shows. */
        static juce::File writeVaryingFile (const juce::String& name, double seconds, int numChannels)
        {
            constexpr double rate = 48000.0;
            auto file = getTestDir().getChildFile (name + ".wav");
            file.deleteFile();

            juce::AudioBuffer<float> buffer (numChannels, (int) (seconds * rate));

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto t = i / rate;
                    const auto shape = 0.5 + 0.5 * std::sin (t * (2.0 + ch) + 0.3 * std::sin (t * 7.0));
                    buffer.setSample (ch, i, (float) (0.8 * shape * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)));
                }

            juce::WavAudioFormat wav;

            if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), rate, (unsigned int) numChannels, 24, {}, 0)))
                {
                    out.release();
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }

            return file;
        }

        /** How many pixels of a waveform show in that part of the view. */
        static int countWaveformPixels (juce::Component& c, juce::Rectangle<int> area)
        {
            auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
            int count = 0;

            for (int y = area.getY(); y < area.getBottom(); ++y)
                for (int x = area.getX(); x < area.getRight(); ++x)
                {
                    auto pixel = data.getPixelColour (x, y);

                    if (pixel.getBrightness() > 0.55f && pixel.getSaturation() < 0.2f)
                        ++count;
                }

            return count;
        }

        void expectSameAsNew (ProjectSession& session, ArrangeView& view, const juce::String& what)
        {
            auto& engine = session.getEdit().engine;
            auto kept = settledSnapshot (view, engine);

            ArrangeView fresh (session, session.getViewState());
            fresh.setBounds (view.getBounds());
            auto rebuilt = settledSnapshot (fresh, engine);

            juce::Rectangle<int> area;
            const int different = countDifferences (kept, rebuilt, area);
            expectEquals (different, 0, what + ": pixels differ from a new view, within " + area.toString());

            // To see what differs: CDAW_SNAPSHOT_DIR=<folder>.
            if (auto folder = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {}); different > 0 && folder.isNotEmpty())
            {
                juce::File dir (folder);
                dir.createDirectory();

                for (auto [image, name] : { std::pair { kept, "paint_kept.png" }, std::pair { rebuilt, "paint_new.png" } })
                {
                    auto file = dir.getChildFile (name);
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    juce::PNGImageFormat().writeImageToStream (image, out);
                }
            }
        }

        void runTest() override
        {
            beginTest ("The waveform stays right through scrolling, zooming and editing");

            CDAWLookAndFeel laf;
            juce::LookAndFeel::setDefaultLookAndFeel (&laf);

            auto dir = getTestDir().getChildFile ("ClipPaint");
            dir.deleteRecursively();
            dir.createDirectory();

            {
                ProjectSession session (getEngine(), ProjectSession::createProjectFolder (dir, "Paint"));
                auto& edit = session.getEdit();
                auto& vs = session.getViewState();
                auto at = [] (double s) { return te::TimePosition::fromSeconds (s); };

                // Stereo (drawn as two lanes) and mono, tall enough to split, and a low track that isn't.
                auto stereoTrack = te::getAudioTracks (edit)[0];
                auto monoTrack = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Mono");
                auto lowTrack = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Low");

                auto stereoClip = stereoTrack->insertWaveClip ("Stereo", writeVaryingFile ("paint_stereo", 60.0, 2),
                                                               { { at (0.0), te::TimeDuration::fromSeconds (60.0) }, {} }, false);
                monoTrack->insertWaveClip ("Mono", writeVaryingFile ("paint_mono", 30.0, 1),
                                           { { at (5.0), te::TimeDuration::fromSeconds (30.0) }, {} }, false);
                lowTrack->insertWaveClip ("Low", writeVaryingFile ("paint_low", 30.0, 2),
                                          { { at (2.0), te::TimeDuration::fromSeconds (30.0) }, {} }, false);

                EditViewState::setTrackHeight (*stereoTrack, 120);
                EditViewState::setTrackHeight (*monoTrack, 120);
                EditViewState::setTrackHeight (*lowTrack, EditViewState::minTrackHeight);

                vs.pixelsPerSecond = 100.0;
                vs.viewStart = 0.0;

                ArrangeView view (session, vs);
                view.setBounds (0, 0, 1000, 500);
                expectSameAsNew (session, view, "As first painted");

                for (double start = 0.5; start < 8.0; start += 0.73)
                {
                    vs.viewStart = start;
                    pump (5);
                    view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                }

                expectSameAsNew (session, view, "Scrolled along in small steps");

                // Far enough along that the columns seen first have been let go of...
                for (double start = 8.0; start < 50.0; start += 3.0)
                {
                    vs.viewStart = start;
                    pump (5);
                    view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                }

                expectSameAsNew (session, view, "Scrolled far along");

                // ...and back to them, from the other side.
                for (double start = 50.0; start > 1.0; start -= 3.3)
                {
                    vs.viewStart = start;
                    pump (5);
                    view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                }

                expectSameAsNew (session, view, "Scrolled back");

                vs.viewStart = 3.0;
                pump (5);
                expectSameAsNew (session, view, "Jumped somewhere it has seen");

                vs.pixelsPerSecond = 37.5;
                pump (5);
                expectSameAsNew (session, view, "Zoomed out");

                vs.pixelsPerSecond = 240.0;
                pump (5);
                expectSameAsNew (session, view, "Zoomed in");

                stereoClip->setOffset (te::TimeDuration::fromSeconds (1.7));

                expectSameAsNew (session, view, "Content slipped");

                dynamic_cast<te::AudioClipBase*> (stereoClip.get())->setGainDB (-9.0f);
                pump (5);
                expectSameAsNew (session, view, "Gain changed");

                beginTest ("The waveform doesn't blink when Tracktion reloads the file after a change");
                {
                    const juce::Rectangle<int> stereoLane (240, 104, 740, 96);
                    auto audio = dynamic_cast<te::AudioClipBase*> (stereoClip.get());
                    audio->setGainDB (-6.0f);
                    pump (2000);
                    const int steady = countWaveformPixels (view, stereoLane);
                    expect (steady > 1000, "There should be a waveform to watch");

                    // Nearly the same setting, so what's drawn should hardly move.
                    audio->setGainDB (-6.001f);
                    pump (5);
                    int lowest = countWaveformPixels (view, stereoLane);

                    for (int i = 0; i < 80; ++i)
                    {
                        pump (20);
                        lowest = juce::jmin (lowest, countWaveformPixels (view, stereoLane));
                    }

                    expectGreaterThan (lowest, steady * 9 / 10, "The waveform blinked");
                }

                beginTest ("A waveform comes back right after its file was reloaded");

                // From one waveform for both channels to one each. (Not the other way: a header
                // shrunk that far keeps its buttons where they were, so it differs from a new one.)
                EditViewState::setTrackHeight (*lowTrack, 120);
                pump (300);
                expectSameAsNew (session, view, "Made tall enough to split the channels");
            }

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }
    };

    static ClipPaintTests clipPaintTests;
}
