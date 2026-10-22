// Builds the example project "Night Drive": a one-minute synth-pop song made only from what ships
// with CDAW (4OSC synths, synthesised drum stems, the built-in effects).
// Runs only when CDAW_MAKE_EXAMPLE is set to the folder the project should go in, e.g.
//   set CDAW_MAKE_EXAMPLE=%USERPROFILE%\Documents\CDAW Projects
// It balances the mix by rendering each part on its own, then renders a preview into Bounces.

#include "TestEngine.h"
#include "Model/ArrangerOps.h"
#include "Model/MixerOps.h"
#include "Model/ProjectSession.h"
#include "Model/TrackOps.h"
#include "Plugins/BuiltInPlugins.h"
#include "Plugins/CompressorPlugin.h"
#include "Plugins/EqPlugin.h"
#include "Plugins/ReverbPlugin.h"

namespace cdaw::test
{
    namespace
    {
        constexpr double bpm = 96.0, sampleRate = 48000.0;
        constexpr int beatsPerBar = 4;

        // Song form, in bars.
        constexpr int introStart = 0, verseStart = 4, chorusStart = 12, outroStart = 20, songBars = 24, tailBars = 2;

        double barsToSeconds (double bars)     { return bars * beatsPerBar * 60.0 / bpm; }

        //==============================================================================
        // Drum synthesis: each hit is added into a stereo buffer at a time in beats.

        struct DrumKit
        {
            explicit DrumKit (int numSamples) : kick (2, numSamples), snare (2, numSamples), hats (2, numSamples)
            {
                for (auto* b : { &kick, &snare, &hats })
                    b->clear();
            }

            static int at (double beat)     { return (int) std::round (beat * 60.0 / bpm * sampleRate); }

            void addKick (double beat, float gain)
            {
                // A sine dropping from 150 Hz to 48 Hz, with a short click on top.
                const int start = at (beat), length = (int) (0.45 * sampleRate);
                double phase = 0.0;
                for (int i = 0; i < length && start + i < kick.getNumSamples(); ++i)
                {
                    const double t = i / sampleRate;
                    const double freq = 48.0 + 102.0 * std::exp (-t * 28.0);
                    phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
                    const double body = std::sin (phase) * std::exp (-t * 7.5);
                    const double click = (i < 96 ? random.nextFloat() * 2.0 - 1.0 : 0.0) * 0.25 * std::exp (-t * 400.0);
                    const auto v = (float) ((body + click) * gain);
                    kick.addSample (0, start + i, v);
                    kick.addSample (1, start + i, v);
                }
            }

            void addSnare (double beat, float gain, float pan = 0.0f)
            {
                // A 190 Hz tone and band-limited noise, decaying fast; the noise slightly wider.
                const int start = at (beat), length = (int) (0.32 * sampleRate);
                float lowL = 0, lowR = 0, prevL = 0, prevR = 0;
                for (int i = 0; i < length && start + i < snare.getNumSamples(); ++i)
                {
                    const double t = i / sampleRate;
                    const double tone = std::sin (juce::MathConstants<double>::twoPi * 190.0 * t) * std::exp (-t * 30.0) * 0.6;
                    auto noise = [&] (float& low, float& prev)
                    {
                        const float n = random.nextFloat() * 2.0f - 1.0f;
                        low += 0.35f * (n - low);                   // gentle low-pass
                        const float hp = low - prev;                // then remove the lows
                        prev = low;
                        return hp * 2.2f * (float) std::exp (-t * 14.0);
                    };
                    const float nl = noise (lowL, prevL), nr = noise (lowR, prevR);
                    snare.addSample (0, start + i, (float) (tone + nl) * gain * (1.0f - pan));
                    snare.addSample (1, start + i, (float) (tone + nr) * gain * (1.0f + pan));
                }
            }

            void addHat (double beat, float gain, bool open, float pan)
            {
                const int start = at (beat), length = (int) ((open ? 0.35 : 0.06) * sampleRate);
                const double decay = open ? 9.0 : 70.0;
                float prev = 0;
                for (int i = 0; i < length && start + i < hats.getNumSamples(); ++i)
                {
                    const double t = i / sampleRate;
                    const float n = random.nextFloat() * 2.0f - 1.0f;
                    const float hp = n - prev * 0.92f;              // bright: mostly the top
                    prev = n;
                    const float v = hp * gain * 0.5f * (float) std::exp (-t * decay);
                    hats.addSample (0, start + i, v * (1.0f - pan));
                    hats.addSample (1, start + i, v * (1.0f + pan));
                }
            }

            juce::AudioBuffer<float> kick, snare, hats;
            juce::Random random { 2026 };
        };

        void writeWav (juce::AudioBuffer<float>& buffer, const juce::File& file)
        {
            // Each stem peaks at -1 dBFS; the faders do the balancing.
            if (auto peak = buffer.getMagnitude (0, buffer.getNumSamples()); peak > 0.0f)
                buffer.applyGain (juce::Decibels::decibelsToGain (-1.0f) / peak);

            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), sampleRate, (unsigned int) buffer.getNumChannels(), 24, {}, 0)))
                {
                    out.release();
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }
        }

        void writeDrums (DrumKit& kit)
        {
            for (int bar = 0; bar < songBars; ++bar)
            {
                const double b = bar * beatsPerBar;
                const bool intro = bar < verseStart, verse = bar >= verseStart && bar < chorusStart;
                const bool chorus = bar >= chorusStart && bar < outroStart, outro = bar >= outroStart;
                const float outroFade = outro ? 1.0f - 0.2f * (float) (bar - outroStart) : 1.0f;

                // Kick
                if (intro && bar >= 2)                  kit.addKick (b, 0.8f);
                if (verse)                              { kit.addKick (b, 0.95f); kit.addKick (b + 1.5, 0.75f); kit.addKick (b + 2.0, 0.9f); }
                if (chorus)                             for (int k = 0; k < 4; ++k) kit.addKick (b + k, 0.95f);
                if (chorus && bar % 2 == 1)             kit.addKick (b + 3.75, 0.6f);
                if (outro && bar < outroStart + 2)      { kit.addKick (b, 0.9f * outroFade); kit.addKick (b + 2.0, 0.8f * outroFade); }

                // Snare on 2 and 4, a roll into the chorus.
                if (verse || chorus)
                {
                    kit.addSnare (b + 1, 0.8f);
                    kit.addSnare (b + 3, 0.85f);
                }

                if (bar == chorusStart - 1)
                    for (int s = 0; s < 4; ++s)
                        kit.addSnare (b + 3 + s * 0.25, 0.35f + 0.12f * (float) s, s % 2 ? 0.15f : -0.15f);

                if (bar == outroStart)
                    kit.addSnare (b + 3, 0.6f);

                // Hats: eighths (intro, verse, outro), sixteenths with open hats in the chorus.
                if (chorus)
                {
                    for (int s = 0; s < 16; ++s)
                    {
                        const bool open = s % 4 == 2;
                        kit.addHat (b + s * 0.25, s % 2 == 0 ? 0.55f : 0.3f, open, s % 2 ? 0.25f : -0.2f);
                    }
                }
                else if (bar >= 1)
                {
                    for (int e = 0; e < 8; ++e)
                        kit.addHat (b + e * 0.5, (e % 2 == 0 ? 0.45f : 0.28f) * (intro ? 0.7f : 1.0f) * outroFade, false, e % 2 ? 0.2f : -0.15f);
                }
            }
        }

        //==============================================================================
        // Music: A minor, Am - F - C - G.

        struct Chord { int bass; std::array<int, 4> pad; std::array<int, 3> arp; };

        const Chord& chordForBar (int bar)
        {
            static const Chord chords[4] {
                { 33, { 57, 60, 64, 71 }, { 69, 72, 76 } },   // Am (add9)
                { 29, { 53, 57, 60, 67 }, { 65, 69, 72 } },   // F (add9)
                { 36, { 55, 60, 64, 67 }, { 67, 72, 76 } },   // C
                { 31, { 55, 59, 62, 69 }, { 67, 71, 74 } },   // G (add9)
            };
            return chords[bar % 4];
        }

        struct Note { double beat, length; int key, velocity; };

        std::vector<Note> padNotes()
        {
            // A chord per bar; the final A minor rings on through the last bars and the tail.
            std::vector<Note> notes;
            for (int bar = introStart; bar < songBars - 2; ++bar)
                for (auto key : chordForBar (bar).pad)
                    notes.push_back ({ bar * 4.0, 4.0, key, 80 });

            for (auto key : chordForBar (0).pad)
                notes.push_back ({ (songBars - 2) * 4.0, 10.0, key, 80 });

            return notes;
        }

        std::vector<Note> bassNotes()
        {
            std::vector<Note> notes;
            for (int bar = 2; bar < songBars - 1; ++bar)
            {
                const double b = bar * 4.0;
                const int root = chordForBar (bar).bass;

                if (bar < verseStart || bar >= outroStart)
                {
                    notes.push_back ({ b, bar == songBars - 2 ? 8.0 : 4.0, bar >= songBars - 2 ? 33 : root, 90 });
                }
                else if (bar < chorusStart)
                {
                    for (int e = 0; e < 8; ++e)        // driving eighths, the octave on the last one
                        notes.push_back ({ b + e * 0.5, 0.42, e == 7 ? root + 12 : root, e % 2 ? 78 : 96 });
                }
                else
                {
                    // Chorus: syncopated, with an octave jump.
                    notes.push_back ({ b,        0.7,  root,      104 });
                    notes.push_back ({ b + 0.75, 0.25, root,      80 });
                    notes.push_back ({ b + 1.5,  0.45, root,      92 });
                    notes.push_back ({ b + 2.0,  0.7,  root,      100 });
                    notes.push_back ({ b + 2.75, 0.25, root + 12, 84 });
                    notes.push_back ({ b + 3.0,  0.45, root,      92 });
                    notes.push_back ({ b + 3.5,  0.45, root + 12, 88 });
                }
            }
            return notes;
        }

        std::vector<Note> leadNotes()
        {
            std::vector<Note> notes;

            // Verse: an arpeggio up and down the chord, in eighths.
            for (int bar = verseStart; bar < chorusStart; ++bar)
            {
                const auto& arp = chordForBar (bar).arp;
                static constexpr int pattern[8] { 0, 1, 2, 1, 0, 1, 2, 1 };
                for (int e = 0; e < 8; ++e)
                    notes.push_back ({ bar * 4.0 + e * 0.5, 0.4, arp[(size_t) pattern[e]] + (bar >= verseStart + 4 && e >= 6 ? 12 : 0),
                                       e == 0 ? 96 : 70 + (e % 2) * 6 });
            }

            // Chorus: the melody, twice with a different ending.
            struct M { double beat; double length; int key; };
            static const M phrase[] {
                // bar 1 (Am)
                { 0.0, 1.0, 76 }, { 1.0, 0.5, 74 }, { 1.5, 0.5, 72 }, { 2.0, 1.0, 74 }, { 3.0, 1.0, 76 },
                // bar 2 (F)
                { 4.0, 2.0, 72 }, { 6.0, 1.0, 69 }, { 7.0, 1.0, 72 },
                // bar 3 (C)
                { 8.0, 1.0, 67 }, { 9.0, 1.0, 72 }, { 10.0, 1.0, 76 }, { 11.0, 1.0, 79 },
            };
            static const std::vector<M> endingA { { 12.0, 3.0, 74 }, { 15.0, 1.0, 71 } };                              // G: D... B
            static const std::vector<M> endingB { { 12.0, 1.0, 74 }, { 13.0, 1.0, 76 }, { 14.0, 2.0, 79 } };         // G: D E G

            for (int half = 0; half < 2; ++half)
            {
                const double start = (chorusStart + half * 4) * 4.0;
                for (auto& m : phrase)
                    notes.push_back ({ start + m.beat, m.length * 0.95, m.key, m.beat == 0.0 ? 104 : 88 });
                for (auto& m : (half == 0 ? endingA : endingB))
                    notes.push_back ({ start + m.beat, m.length * 0.95, m.key, 96 });
            }

            // Outro: the tune's first note, echoing out.
            notes.push_back ({ outroStart * 4.0, 2.0, 81, 90 });
            notes.push_back ({ outroStart * 4.0 + 6.0, 2.0, 76, 70 });
            return notes;
        }

        //==============================================================================
        te::FourOscPlugin* addSynth (te::AudioTrack& track)
        {
            for (auto p : track.pluginList.getPlugins())
                if (auto synth = dynamic_cast<te::FourOscPlugin*> (p))
                    return synth;
            return nullptr;
        }

        void set (te::AutomatableParameter::Ptr p, float v)     { p->setParameter (v, juce::sendNotification); }

        void soundBass (te::FourOscPlugin& s)
        {
            s.oscParams[0]->waveShapeValue = 3;                 // saw
            s.oscParams[1]->waveShapeValue = 2;                 // square, an octave down
            set (s.oscParams[1]->tune, -12.0f);
            set (s.oscParams[1]->level, -6.0f);
            s.filterTypeValue = 1;                              // low-pass
            set (s.filterFreq, 52.0f);
            set (s.filterResonance, 18.0f);
            set (s.filterAmount, 0.35f);
            set (s.filterAttack, 0.0f);
            set (s.filterDecay, 0.22f);
            set (s.filterSustain, 20.0f);
            set (s.ampAttack, 0.003f);
            set (s.ampDecay, 0.3f);
            set (s.ampSustain, 75.0f);
            set (s.ampRelease, 0.07f);
            set (s.masterLevel, -6.0f);
        }

        void soundPad (te::FourOscPlugin& s)
        {
            s.oscParams[0]->waveShapeValue = 3;
            s.oscParams[0]->voicesValue = 3;
            set (s.oscParams[0]->detune, 0.14f);
            set (s.oscParams[0]->spread, 70.0f);
            s.oscParams[1]->waveShapeValue = 3;
            s.oscParams[1]->voicesValue = 2;
            set (s.oscParams[1]->tune, 12.0f);
            set (s.oscParams[1]->level, -12.0f);
            set (s.oscParams[1]->detune, 0.1f);
            set (s.oscParams[1]->spread, -60.0f);
            s.filterTypeValue = 1;
            set (s.filterFreq, 84.0f);
            set (s.filterResonance, 12.0f);
            set (s.ampAttack, 0.7f);
            set (s.ampDecay, 1.0f);
            set (s.ampSustain, 85.0f);
            set (s.ampRelease, 1.4f);
            s.chorusOnValue = true;
            set (s.chorusMix, 0.35f);
            set (s.masterLevel, -4.0f);
        }

        void soundLead (te::FourOscPlugin& s)
        {
            // A big supersaw: seven detuned saws spread out, four more an octave up, a square an
            // octave down for weight, driven into the synth's distortion, then chorus and a
            // dotted-eighth delay bouncing between left and right.
            s.oscParams[0]->waveShapeValue = 3;
            s.oscParams[0]->voicesValue = 7;
            set (s.oscParams[0]->detune, 0.22f);
            set (s.oscParams[0]->spread, 35.0f);

            s.oscParams[1]->waveShapeValue = 3;
            s.oscParams[1]->voicesValue = 4;
            set (s.oscParams[1]->tune, 12.0f);
            set (s.oscParams[1]->level, -8.0f);
            set (s.oscParams[1]->detune, 0.16f);
            set (s.oscParams[1]->spread, -25.0f);

            s.oscParams[2]->waveShapeValue = 2;
            set (s.oscParams[2]->tune, -12.0f);
            set (s.oscParams[2]->level, -10.0f);

            s.filterTypeValue = 1;
            set (s.filterFreq, 98.0f);
            set (s.filterResonance, 10.0f);
            set (s.filterAmount, 0.15f);
            set (s.filterDecay, 0.6f);
            set (s.filterSustain, 70.0f);

            set (s.ampAttack, 0.012f);
            set (s.ampDecay, 0.6f);
            set (s.ampSustain, 85.0f);
            set (s.ampRelease, 0.45f);
            set (s.ampVelocity, 60.0f);

            s.distortionOnValue = true;
            set (s.distortion, 0.32f);

            s.chorusOnValue = true;
            set (s.chorusMix, 0.15f);

            s.delayOnValue = true;
            s.delayValue = 0.75f;                               // a dotted eighth (in beats)
            set (s.delayFeedback, -9.0f);
            set (s.delayCrossfeed, -4.0f);                      // echoes bounce between the sides
            set (s.delayMix, 0.28f);

            set (s.masterLevel, -19.0f);
        }

        /** Renders the edit (only `tracks` and their parent groups when given) at 48 kHz in 32-bit
            float, so levels above 0 dBFS come back as they are. */
        juce::AudioBuffer<float> render (te::Edit& edit, juce::Array<te::Track*> tracks = {})
        {
            for (int i = tracks.size(); --i >= 0;)
                if (auto parent = tracks[i]->getParentFolderTrack())
                    tracks.addIfNotAlreadyThere (parent);

            juce::TemporaryFile temp (".wav");
            te::Renderer::Parameters params (edit);
            params.destFile = temp.getFile();
            params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
            params.bitDepth = 32;
            params.sampleRateForAudio = sampleRate;
            params.time = te::TimeRange (te::TimePosition(), edit.getLength());
            params.tracksToDo = te::toBitSet (tracks.isEmpty() ? te::getAllTracks (edit) : tracks);
            params.useMasterPlugins = tracks.isEmpty();     // the full mix through the master chain; parts before it
            te::Renderer::renderToFile ("Example song", params);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioBuffer<float> buffer;
            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (temp.getFile())))
            {
                buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
            }
            return buffer;
        }

        float peakDb (const juce::AudioBuffer<float>& b)    { return juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -150.0f); }

        float rmsDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec, double rate)
        {
            auto s = (int) (fromSec * rate), e = juce::jmin (b.getNumSamples(), (int) (toSec * rate));
            float sum = 0.0f;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                sum += b.getRMSLevel (ch, s, e - s);
            return juce::Decibels::gainToDecibels (sum / (float) b.getNumChannels(), -150.0f);
        }
    }

    //==============================================================================
    struct ExampleSong : public juce::UnitTest
    {
        ExampleSong() : juce::UnitTest ("Example song", "CDAW") {}

        void runTest() override
        {
            auto target = juce::SystemStats::getEnvironmentVariable ("CDAW_MAKE_EXAMPLE", {});
            if (target.isEmpty())
                return;

            beginTest ("Build the example project \"Night Drive\"");

            auto& engine = getEngine();
            auto parent = juce::File (target);
            parent.createDirectory();
            auto existing = parent.getChildFile ("Night Drive");
            expect (! existing.exists(), "won't overwrite an existing \"Night Drive\" folder");
            if (existing.exists())
                return;

            auto editFile = ProjectSession::createProjectFolder (parent, "Night Drive");
            ProjectSession session (engine, editFile);
            auto& edit = session.getEdit();
            auto& ts = edit.tempoSequence;
            ts.getTempo (0)->setBpm (bpm);
            auto beat = [&] (double b) { return ts.toTime (te::BeatPosition::fromBeats (b)); };
            auto bars = [&] (double from, double to) { return te::TimeRange (beat (from * beatsPerBar), beat (to * beatsPerBar)); };

            // The new project's default track is replaced by the song's own.
            {
                juce::Array<te::Track*> old;
                for (auto t : te::getAudioTracks (edit)) old.add (t);
                TrackOps::deleteTracks (edit, old);
            }

            // Song sections.
            ArrangerOps::addSection (edit, bars (introStart, verseStart), "Intro");
            ArrangerOps::addSection (edit, bars (verseStart, chorusStart), "Verse");
            ArrangerOps::addSection (edit, bars (chorusStart, outroStart), "Chorus");
            ArrangerOps::addSection (edit, bars (outroStart, songBars), "Outro");

            // Drum stems, one per drum, the length of the song plus a tail.
            const int length = (int) std::ceil (barsToSeconds (songBars + tailBars) * sampleRate);
            DrumKit kit (length);
            writeDrums (kit);
            auto audio = session.getAudioFolder();
            audio.createDirectory();
            writeWav (kit.kick, audio.getChildFile ("Kick.wav"));
            writeWav (kit.snare, audio.getChildFile ("Snare.wav"));
            writeWav (kit.hats, audio.getChildFile ("Hats.wav"));

            // FX return with C-Verb (fully wet).
            auto fx = TrackOps::addFxTrack (edit, nullptr);
            fx->setName ("Reverb");
            auto reverbPlugin = edit.getPluginCache().createNewPlugin (ReverbPlugin::xmlTypeName, {});
            fx->pluginList.insertPlugin (reverbPlugin, 0, nullptr);
            auto* reverb = dynamic_cast<ReverbPlugin*> (reverbPlugin.get());
            reverb->mix.setParameter (1.0f, juce::sendNotification);
            reverb->decay.setParameter (2.8f, juce::sendNotification);
            reverb->preDelay.setParameter (28.0f, juce::sendNotification);
            reverb->size.setParameter (0.7f, juce::sendNotification);
            reverb->postLowCut.setParameter (220.0f, juce::sendNotification);
            reverb->postHighCut.setParameter (9000.0f, juce::sendNotification);
            const int reverbBus = MixerOps::getFxBuses (edit).getFirst().number;

            // Drums: a group with kick, snare and hats inside, glued by a compressor.
            auto drums = TrackOps::addFolderTrack (edit, nullptr, true);
            drums->setName ("Drums");
            drums->setColour (juce::Colour (0xffe5383b));
            {
                auto comp = edit.getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});
                drums->pluginList.insertPlugin (comp, 0, nullptr);
                auto* c = dynamic_cast<CompressorPlugin*> (comp.get());
                c->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-16.0f), juce::sendNotification);
                c->ratio.setParameter (1.0f / 3.0f, juce::sendNotification);
                c->attackMs.setParameter (12.0f, juce::sendNotification);
                c->releaseMs.setParameter (120.0f, juce::sendNotification);
                c->outputDb.setParameter (2.0f, juce::sendNotification);
            }

            std::map<juce::String, te::AudioTrack::Ptr> parts;
            te::Track* previous = nullptr;
            const std::pair<const char*, juce::Colour> drumParts[] { { "Kick", juce::Colour (0xffe5484d) }, { "Snare", juce::Colour (0xffff6f61) },
                                                                     { "Hats", juce::Colour (0xffff9a8a) } };
            for (auto [name, colour] : drumParts)
            {
                auto t = TrackOps::addAudioTrack (edit, nullptr, nullptr, name);
                t->setColour (colour);
                edit.moveTrack (t, te::TrackInsertPoint (drums.get(), previous));
                t->insertWaveClip (name, audio.getChildFile (juce::String (name) + ".wav"), { bars (0, songBars + tailBars), {} }, false);
                parts[name] = t;
                previous = t.get();
            }
            MixerOps::addSend (*parts["Snare"], reverbBus, true, -12.0f);
            parts["Hats"]->getVolumePlugin()->setPan (0.1f);

            // Synths.
            auto addInstrument = [&] (const char* name, juce::Colour colour)
            {
                auto desc = te::PluginManager::createBuiltInPluginDescription<te::FourOscPlugin> (true);
                auto t = TrackOps::addInstrumentTrack (edit, nullptr, &desc);
                t->setName (name);
                t->setColour (colour);
                parts[name] = t;
                return t;
            };

            auto bass = addInstrument ("Bass", juce::Colour (0xff4fa3ff));
            auto pad = addInstrument ("Pad", juce::Colour (0xffd77cff));
            auto lead = addInstrument ("Lead", juce::Colour (0xfff2b134));
            soundBass (*addSynth (*bass));
            soundPad (*addSynth (*pad));
            soundLead (*addSynth (*lead));

            auto addNotes = [&] (te::AudioTrack& t, const std::vector<Note>& notes, const juce::String& clipName)
            {
                auto clip = t.insertMIDIClip (bars (0, songBars + 1), nullptr);
                clip->setName (clipName);
                for (auto& n : notes)
                    clip->getSequence().addNote (n.key, te::BeatPosition::fromBeats (n.beat), te::BeatDuration::fromBeats (n.length),
                                                 n.velocity, 0, nullptr);
            };

            addNotes (*bass, bassNotes(), "Bass");
            addNotes (*pad, padNotes(), "Chords");
            addNotes (*lead, leadNotes(), "Lead");

            // The reverb return goes below the instruments.
            edit.moveTrack (fx, te::TrackInsertPoint (nullptr, lead.get()));

            // Bass: low cut and a little weight; pad: thinned under the bass. Sends to the reverb.
            {
                auto eqPlugin = edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                bass->pluginList.insertPlugin (eqPlugin, 1, nullptr);
                auto* eq = dynamic_cast<EqPlugin*> (eqPlugin.get());
                eq->setType (0, EqMath::BandType::lowCut);
                eq->getBand (0).freq.setParameter (32.0f, juce::sendNotification);
                eq->setOn (0, true);
                eq->setType (2, EqMath::BandType::peak);
                eq->getBand (2).freq.setParameter (90.0f, juce::sendNotification);
                eq->getBand (2).gain.setParameter (2.5f, juce::sendNotification);
                eq->setOn (2, true);

                auto padEqPlugin = edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                pad->pluginList.insertPlugin (padEqPlugin, 1, nullptr);
                auto* padEq = dynamic_cast<EqPlugin*> (padEqPlugin.get());
                padEq->setType (0, EqMath::BandType::lowCut);
                padEq->getBand (0).freq.setParameter (180.0f, juce::sendNotification);
                padEq->setOn (0, true);
            }
            {
                // Lead: lows out (the bass has them), a little presence, the fizz of the
                // distortion softened; then a big, long reverb of its own.
                auto eqPlugin = edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                lead->pluginList.insertPlugin (eqPlugin, 1, nullptr);
                auto* eq = dynamic_cast<EqPlugin*> (eqPlugin.get());
                eq->setType (0, EqMath::BandType::lowCut);
                eq->getBand (0).freq.setParameter (220.0f, juce::sendNotification);
                eq->setOn (0, true);
                eq->setType (4, EqMath::BandType::peak);
                eq->getBand (4).freq.setParameter (2800.0f, juce::sendNotification);
                eq->getBand (4).gain.setParameter (2.5f, juce::sendNotification);
                eq->getBand (4).q.setParameter (0.9f, juce::sendNotification);
                eq->setOn (4, true);
                eq->setType (6, EqMath::BandType::highShelf);
                eq->getBand (6).freq.setParameter (7500.0f, juce::sendNotification);
                eq->getBand (6).gain.setParameter (-4.0f, juce::sendNotification);
                eq->setOn (6, true);

                auto verbPlugin = edit.getPluginCache().createNewPlugin (ReverbPlugin::xmlTypeName, {});
                lead->pluginList.insertPlugin (verbPlugin, 2, nullptr);
                auto* verb = dynamic_cast<ReverbPlugin*> (verbPlugin.get());
                verb->mix.setParameter (0.18f, juce::sendNotification);
                verb->decay.setParameter (4.2f, juce::sendNotification);
                verb->preDelay.setParameter (70.0f, juce::sendNotification);
                verb->size.setParameter (0.9f, juce::sendNotification);
                verb->width.setParameter (1.0f, juce::sendNotification);
                verb->modulation.setParameter (0.5f, juce::sendNotification);
                verb->postLowCut.setParameter (320.0f, juce::sendNotification);
                verb->postHighCut.setParameter (7000.0f, juce::sendNotification);
            }

            MixerOps::addSend (*pad, reverbBus, true, -8.0f);
            MixerOps::addSend (*lead, reverbBus, true, -14.0f);
            lead->getVolumePlugin()->setPan (-0.1f);

            // The pad's filter opens through the intro and closes again in the outro.
            {
                auto& curve = addSynth (*pad)->filterFreq->getCurve();
                curve.addPoint (beat (0.0), 58.0f, 0.0f, nullptr);
                curve.addPoint (beat (verseStart * 4.0), 84.0f, 0.0f, nullptr);
                curve.addPoint (beat (outroStart * 4.0), 84.0f, 0.0f, nullptr);
                curve.addPoint (beat ((songBars + 1) * 4.0), 56.0f, 0.0f, nullptr);
            }

            // Master: a touch of air, then gentle glue.
            {
                auto eqPlugin = edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                edit.getMasterPluginList().insertPlugin (eqPlugin, 0, nullptr);
                auto* eq = dynamic_cast<EqPlugin*> (eqPlugin.get());
                eq->setType (6, EqMath::BandType::highShelf);
                eq->getBand (6).freq.setParameter (10000.0f, juce::sendNotification);
                eq->getBand (6).gain.setParameter (1.5f, juce::sendNotification);
                eq->setOn (6, true);

                auto comp = edit.getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});
                edit.getMasterPluginList().insertPlugin (comp, 1, nullptr);
                auto* c = dynamic_cast<CompressorPlugin*> (comp.get());
                c->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-10.0f), juce::sendNotification);
                c->ratio.setParameter (0.5f, juce::sendNotification);
                c->attackMs.setParameter (30.0f, juce::sendNotification);
                c->releaseMs.setParameter (200.0f, juce::sendNotification);
            }

            // Mix: render each part on its own and set its fader to a target level. Drums by their
            // peaks (short hits: their average says little), the sustained synths by their average
            // level through the chorus.
            const double chorusFrom = barsToSeconds (chorusStart), chorusTo = barsToSeconds (outroStart);
            struct Target { const char* name; float db; bool byPeak; };
            const Target targets[] { { "Kick", -6.0f, true }, { "Snare", -9.0f, true }, { "Hats", -17.0f, true },
                                     { "Bass", -19.0f, false }, { "Pad", -24.0f, false }, { "Lead", -20.0f, false } };

            for (auto& [name, targetDb, byPeak] : targets)
            {
                auto t = parts[name];
                auto alone = render (edit, { t.get() });
                auto level = byPeak ? peakDb (alone) : rmsDb (alone, chorusFrom, chorusTo, sampleRate);
                auto fader = juce::jlimit (-30.0f, 6.0f, targetDb - level);
                t->getVolumePlugin()->setVolumeDb (fader);
                logMessage (juce::String (name) + ": " + juce::String (level, 1) + (byPeak ? " dB peak" : " dB RMS") + " alone, fader " + juce::String (fader, 1) + " dB");
            }

            // How wide the lead is: side against mid through the chorus (above about -3 dB it starts
            // to lose body in mono).
            {
                auto alone = render (edit, { lead.get() });
                const int from = (int) (chorusFrom * sampleRate), to = (int) (chorusTo * sampleRate);
                double mid = 0, side = 0;
                for (int i = from; i < to; ++i)
                {
                    auto l = alone.getSample (0, i), r = alone.getSample (1, i);
                    mid += 0.25 * (l + r) * (l + r);
                    side += 0.25 * (l - r) * (l - r);
                }
                logMessage ("Lead: side " + juce::String (10.0 * std::log10 (side / mid), 1) + " dB against mid");
            }

            // Full mix: aim the chorus at about -12 dB RMS, then back off until peaks stay under -1 dBFS
            // (renders clip at 0 dBFS, so a peak that reaches it says nothing about how far over it was).
            auto master = edit.getMasterVolumePlugin();
            auto mix = render (edit);
            auto gain = juce::jlimit (-24.0f, 6.0f, -13.0f - rmsDb (mix, chorusFrom, chorusTo, sampleRate));

            for (int attempt = 0; attempt < 6; ++attempt)
            {
                master->setVolumeDb (gain);
                mix = render (edit);
                auto peak = peakDb (mix);
                logMessage ("Master " + juce::String (gain, 1) + " dB: chorus " + juce::String (rmsDb (mix, chorusFrom, chorusTo, sampleRate), 1)
                            + " dB RMS, peak " + juce::String (peak, 1) + " dBFS");
                // The supersaw's free-running phases change the peaks a little from render to render.
                if (peak < -2.5f)
                    break;
                gain -= peak + 2.7f;      // float render: the real overshoot
            }

            // A gentle fade at the very end.
            {
                auto& curve = master->volParam->getCurve();
                auto level = te::decibelsToVolumeFaderPosition (gain);
                curve.addPoint (beat ((songBars - 1) * 4.0), level, 0.0f, nullptr);
                curve.addPoint (beat ((songBars + tailBars) * 4.0), te::decibelsToVolumeFaderPosition (-40.0f), 0.0f, nullptr);
            }

            expect (session.save(), "project saved");

            // A preview to listen to straight away.
            auto preview = render (edit);
            auto finalPeak = peakDb (preview);
            logMessage ("Preview: " + juce::String (preview.getNumSamples() / sampleRate, 1) + " s, peak "
                        + juce::String (finalPeak, 1) + " dBFS, chorus " + juce::String (rmsDb (preview, chorusFrom, chorusTo, sampleRate), 1) + " dB RMS");
            expectLessThan (finalPeak, -0.5f, "no clipping");

            session.getBouncesFolder().createDirectory();
            juce::AudioBuffer<float> out (preview);
            juce::WavAudioFormat wav;
            auto file = session.getBouncesFolder().getChildFile ("Night Drive (preview).wav");
            file.deleteFile();
            if (auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
                }

            logMessage ("Example project: " + editFile.getFullPathName());
        }
    };

    static ExampleSong exampleSong;
}
