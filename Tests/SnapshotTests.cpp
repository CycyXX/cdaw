// Renders the main UI components offscreen to PNGs for visual inspection.
// Enabled with CDAW_SNAPSHOTS=1; images go to %TEMP%\CDAW_Tests\snapshots.

#include "TestEngine.h"
#include "Model/ClipOps.h"
#include "Model/DrumMapLibrary.h"
#include "Model/EditViewState.h"
#include "Model/MixerOps.h"
#include "Model/AutomationOps.h"
#include "Model/ArrangerOps.h"
#include "Engine/MasterAnalyser.h"
#include "Model/CompOps.h"
#include "Model/ProjectSession.h"
#include "Model/TrackOps.h"
#include "UI/Arrange/ArrangeView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Mixer/MixerView.h"
#include "UI/Mixer/SpectrumView.h"
#include "UI/PianoRoll/PianoRoll.h"

namespace cdaw::test
{
    struct SnapshotTests : public juce::UnitTest
    {
        SnapshotTests() : juce::UnitTest ("UI snapshots", "CDAW") {}

        static void save (juce::Component& c, const juce::String& name)
        {
            auto dir = getTestDir().getChildFile ("snapshots");
            dir.createDirectory();
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            auto file = dir.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            png.writeImageToStream (img, out);
        }

        void runTest() override
        {
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Render arrangement, piano roll, drum editor and mixer");

            CDAWLookAndFeel laf;
            juce::LookAndFeel::setDefaultLookAndFeel (&laf);

            auto& engine = getEngine();
            auto dir = getTestDir().getChildFile ("SnapshotProject");
            dir.deleteRecursively();
            dir.createDirectory();
            auto editFile = ProjectSession::createProjectFolder (dir, "Demo");

            {
                ProjectSession session (engine, editFile);
                auto& edit = session.getEdit();
                auto& vs = session.getViewState();

                // Guitar track with audio
                auto gtr = te::getAudioTracks (edit)[0];
                gtr->setName ("Rhythm L");
                gtr->setColour (juce::Colour (0xff4fa3ff));
                auto riff = writeSineFile ("snap_riff", 8.0, 0.4f, 82.0);
                ClipOps::importAudioFiles (session, { riff }, gtr, te::TimePosition::fromSeconds (2.0), false);

                // Drum track with a MIDI groove + GM drum map
                auto drums = TrackOps::addInstrumentTrack (edit, nullptr, nullptr);
                drums->setName ("Drums (SSD5)");
                drums->state.setProperty (ViewIDs::cdawDrumMap, "GM", nullptr);
                auto mc = drums->insertMIDIClip ({ te::TimePosition(), te::TimePosition::fromSeconds (8.0) }, nullptr);
                auto& seq = mc->getSequence();

                for (int beat = 0; beat < 16; ++beat)
                {
                    seq.addNote (42, te::BeatPosition::fromBeats (beat * 0.5), te::BeatDuration::fromBeats (0.25), 90, 0, nullptr);
                    seq.addNote (42, te::BeatPosition::fromBeats (beat * 0.5 + 0.25), te::BeatDuration::fromBeats (0.25), 60, 0, nullptr);
                    if (beat % 2 == 0) seq.addNote (36, te::BeatPosition::fromBeats (beat * 0.5), te::BeatDuration::fromBeats (0.25), 120, 0, nullptr);
                    if (beat % 4 == 2) seq.addNote (38, te::BeatPosition::fromBeats (beat * 0.5), te::BeatDuration::fromBeats (0.25), 115, 0, nullptr);
                }

                // Synth track with chords (piano roll mode)
                auto synth = TrackOps::addInstrumentTrack (edit, nullptr, nullptr);
                synth->setName ("Pad");
                auto mc2 = synth->insertMIDIClip ({ te::TimePosition::fromSeconds (4.0), te::TimePosition::fromSeconds (12.0) }, nullptr);
                for (int bar = 0; bar < 4; ++bar)
                    for (int n : { 48, 55, 60, 64 })
                        mc2->getSequence().addNote (n + (bar % 2) * 2, te::BeatPosition::fromBeats (bar * 4.0), te::BeatDuration::fromBeats (3.5), 90, 0, nullptr);

                auto fx = TrackOps::addFxTrack (edit, nullptr);
                MixerOps::addSend (*gtr, MixerOps::getFxBuses (edit)[0].number, true);
                TrackOps::addFolderTrack (edit, nullptr, true);

                vs.pixelsPerSecond = 60.0;

                // A loop-recorded lead with three takes, lanes shown, partly comped.
                {
                    auto lead = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Lead");
                    auto t1 = writeSineFile ("snap_t1", 4.0, 0.2f, 220.0);
                    auto t2 = writeSineFile ("snap_t2", 4.0, 0.5f, 330.0);
                    auto t3 = writeSineFile ("snap_t3", 4.0, 0.8f, 440.0);
                    auto wc = lead->insertWaveClip ("Lead", t1, { { te::TimePosition::fromSeconds (8.0), te::TimeDuration::fromSeconds (4.0) }, {} }, false);
                    wc->addTake (t1); wc->addTake (t2); wc->addTake (t3);
                    EditViewState::setShowingTakes (*lead, true);
                    CompOps::compRange (*lead, { te::TimePosition::fromSeconds (9.0), te::TimePosition::fromSeconds (10.5) }, 2);
                }

                // Show a volume automation lane with a fade on the guitar.
                if (auto vol = AutomationOps::getVolumeParameter (*gtr))
                {
                    vol->getCurve().addPoint (te::TimePosition::fromSeconds (2.0), te::decibelsToVolumeFaderPosition (0.0f), 0.0f, nullptr);
                    vol->getCurve().addPoint (te::TimePosition::fromSeconds (6.0), te::decibelsToVolumeFaderPosition (-3.0f), 0.0f, nullptr);
                    vol->getCurve().addPoint (te::TimePosition::fromSeconds (9.0), te::decibelsToVolumeFaderPosition (-30.0f), 0.0f, nullptr);
                    AutomationOps::showLane (*gtr, *vol);
                }

                // Song sections, and clip gain / fade shapes on the guitar clip.
                ArrangerOps::addSection (edit, { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, "Intro");
                ArrangerOps::addSection (edit, { te::TimePosition::fromSeconds (2.0), te::TimePosition::fromSeconds (6.0) }, "Verse");
                ArrangerOps::addSection (edit, { te::TimePosition::fromSeconds (6.0), te::TimePosition::fromSeconds (12.0) }, "Chorus");

                if (auto gclip = dynamic_cast<te::AudioClipBase*> (gtr->getClips().getFirst()))
                {
                    gclip->setGainDB (-4.5f);
                    gclip->setFadeIn (te::TimeDuration::fromSeconds (1.0));
                    gclip->setFadeInType (te::AudioFadeCurve::sCurve);
                    gclip->setFadeOut (te::TimeDuration::fromSeconds (1.5));
                    gclip->setFadeOutType (te::AudioFadeCurve::convex);
                }

                {
                    ArrangeView arrange (session, vs);
                    arrange.setBounds (0, 0, 1400, 700);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
                    save (arrange, "arrange");

                    // Zoomed out: the ruler and grid thin out instead of turning into a solid block.
                    for (double pps : { 6.0, 1.5, 0.3 })
                    {
                        vs.pixelsPerSecond = pps;
                        arrange.resized();
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                        save (arrange, "arrange_zoom_" + juce::String (pps, 1));
                    }

                    vs.pixelsPerSecond = 60.0;
                }

                {
                    PianoRoll roll (vs, mc2);
                    roll.setBounds (0, 0, 1200, 420);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    save (roll, "pianoroll");
                }

                {
                    PianoRoll roll (vs, mc);
                    roll.setDrumMap (DrumMapLibrary::find ("GM").value_or (DrumMap::parse ("GM", "36 Kick\n38 Snare\n42 Closed Hi-Hat")));
                    roll.setBounds (0, 0, 1200, 420);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    save (roll, "drumeditor");
                }

                {
                    MixerView mixer (vs);
                    mixer.setBounds (0, 0, 900, 460);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    save (mixer, "mixer");

                    // A taller panel should lengthen the faders, not add empty space.
                    mixer.setBounds (0, 0, 900, 760);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    save (mixer, "mixer_tall");

                    mixer.setBounds (0, 0, 900, 300);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    save (mixer, "mixer_short");

                    // Spectrum beside the master (empty offscreen), plus one fed a test signal.
                    vs.state.setProperty (ViewIDs::showSpectrum, true, nullptr);
                    mixer.setBounds (0, 0, 1300, 460);
                    save (mixer, "mixer_spectrum");

                    auto fifo = std::make_shared<AnalyserFifo>();
                    SpectrumView spectrumView (fifo);
                    spectrumView.setBounds (0, 0, 420, 300);
                    fifo->active = true;
                    juce::AudioBuffer<float> block (2, 4800);
                    juce::Random rng (7);

                    for (int pass = 0; pass < 6; ++pass)
                    {
                        for (int i = 0; i < block.getNumSamples(); ++i)
                        {
                            auto t = (pass * block.getNumSamples() + i) / 48000.0;
                            auto s = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                                   + 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 1760.0 * t)
                                   + 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
                            block.setSample (0, i, s);
                            block.setSample (1, i, s);
                        }

                        fifo->push (block);
                    }

                    // One update with a full history (frames with a half-empty history show the
                    // silence-to-tone step, which takes a moment of real time to fall away).
                    spectrumView.update();
                    save (spectrumView, "spectrum");
                }

                juce::ignoreUnused (fx);

                // Kept, so the demo can be opened in the app (e.g. for the README screenshots).
                session.save();
            }

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            expect (true);
        }
    };

    static SnapshotTests snapshotTests;
}
