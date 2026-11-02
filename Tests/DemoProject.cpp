// Builds the demo project "Harbour Lights", a song made the way a song gets made in CDAW, and
// measures what comes out of it.
//
// Its drums (four mono tracks in a group), guitar and bass are really recorded, through a fake audio
// interface that plays them in: over each other, in a loop and punched in, so they end up as takes.
// They get comped with group editing; pad, lead, effects, sections, lyrics and a copied chorus follow.
// Then every way out of the project is checked by reading the results back: mixdowns in each format,
// normalising, stems, MIDI, DAWproject, versions, templates, presets, and a moved project folder.
//
// Runs only when CDAW_MAKE_DEMO is set to the folder the project should go in, e.g.
//   set CDAW_MAKE_DEMO=%USERPROFILE%\Documents\CDAW Projects
// A project whose folder is there already is left as it is. The checks that change the song run on a
// copy of it, so the project stays as built. Takes a few minutes.

#include "ExampleParts.h"
#include "Model/ArrangerOps.h"
#include "Model/AutomationOps.h"
#include "Model/ClipOps.h"
#include "Model/CompOps.h"
#include "Model/DawProject.h"
#include "Model/EditViewState.h"
#include "Model/GroupEdit.h"
#include "Model/KeyScale.h"
#include "Model/MissingMedia.h"
#include "Model/MixerOps.h"
#include "Model/ProjectSession.h"
#include "Model/ProjectVersions.h"
#include "Model/SongNotes.h"
#include "Model/TemplateOps.h"
#include "Model/TempoOps.h"
#include "Model/TrackOps.h"
#include "Plugins/BuiltInPlugins.h"
#include "Plugins/ChorusPlugin.h"
#include "Plugins/CompressorPlugin.h"
#include "Plugins/DelayPlugin.h"
#include "Plugins/DistPlugin.h"
#include "Plugins/EqPlugin.h"
#include "Plugins/EsserPlugin.h"
#include "Plugins/MultibandPlugin.h"
#include "Plugins/PhaserPlugin.h"
#include "Plugins/ReverbPlugin.h"
#include "Render/ExportService.h"
#include "UI/Arrange/ArrangeView.h"
#include "UI/Export/ExportDialog.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Mixer/MixerView.h"
#include "UI/Notes/NotesPage.h"
#include "UI/PianoRoll/PianoRoll.h"
#include <atomic>
#include <thread>

namespace cdaw::test
{
    using namespace example;

    namespace
    {
        constexpr double bpm = 100.0, rate = 48000.0;
        constexpr int blockSize = 256;
        constexpr double feedSpeed = 4.0;     // recordings run this much faster than real time

        // The form as it's recorded. A second chorus gets copied in at the outro later, moving it back.
        constexpr int verseBar = 4, chorusBar = 12, bridgeBar = 20, outroBar = 24, endBar = 28;
        constexpr int lengthInBars = 36;      // of what's played in (the loop's passes run past the end)

        double barToSeconds (double bar)      { return bar * 4.0 * 60.0 / bpm; }
        double beatToSeconds (double beat)    { return beat * 60.0 / bpm; }

        using Mono = juce::AudioBuffer<float>;

        Mono silence()
        {
            Mono b (1, (int) (barToSeconds (lengthInBars) * rate));
            b.clear();
            return b;
        }

        //==============================================================================
        // D minor: Dm - Bb - F - C.
        struct Chord { int bass; std::array<int, 4> pad; std::array<int, 3> arp; std::array<int, 2> power; };

        const Chord& chordForBar (int bar)
        {
            static const Chord chords[4] {
                { 38, { 62, 65, 69, 72 }, { 74, 77, 81 }, { 50, 57 } },
                { 34, { 58, 62, 65, 69 }, { 70, 74, 77 }, { 46, 53 } },
                { 41, { 60, 65, 69, 72 }, { 72, 77, 81 }, { 53, 60 } },
                { 36, { 60, 64, 67, 74 }, { 72, 76, 79 }, { 48, 55 } },
            };
            return chords[bar % 4];
        }

        struct Note { double beat, length; int key, velocity; };

        std::vector<Note> padNotes()
        {
            std::vector<Note> notes;
            for (int bar = 0; bar < endBar - 2; ++bar)
                for (auto key : chordForBar (bar).pad)
                    notes.push_back ({ bar * 4.0, 4.0, key, 78 });

            for (auto key : chordForBar (0).pad)
                notes.push_back ({ (endBar - 2) * 4.0, 8.0, key, 78 });

            return notes;
        }

        std::vector<Note> leadNotes()
        {
            std::vector<Note> notes;

            for (int bar = verseBar; bar < chorusBar; ++bar)
            {
                const auto& arp = chordForBar (bar).arp;
                static constexpr int pattern[8] { 0, 1, 2, 1, 2, 1, 0, 1 };
                for (int e = 0; e < 8; ++e)
                    notes.push_back ({ bar * 4.0 + e * 0.5, 0.4, arp[(size_t) pattern[e]], e == 0 ? 92 : 66 + (e % 2) * 6 });
            }

            struct M { double beat, length; int key; };
            static const M phrase[] { { 0.0, 1.5, 81 }, { 1.5, 0.5, 79 }, { 2.0, 1.0, 77 }, { 3.0, 1.0, 76 },
                                      { 4.0, 2.0, 77 }, { 6.0, 1.0, 74 }, { 7.0, 1.0, 77 },
                                      { 8.0, 1.5, 72 }, { 9.5, 0.5, 74 }, { 10.0, 1.0, 76 }, { 11.0, 1.0, 77 },
                                      { 12.0, 3.0, 79 }, { 15.0, 1.0, 76 } };

            for (int half = 0; half < 2; ++half)
                for (auto& m : phrase)
                    notes.push_back ({ (chorusBar + half * 4) * 4.0 + m.beat, m.length * 0.95, m.key, m.beat == 0.0 ? 104 : 88 });

            for (int bar = bridgeBar; bar < outroBar; ++bar)
                notes.push_back ({ bar * 4.0, 3.8, chordForBar (bar).arp[2], 74 });

            notes.push_back ({ outroBar * 4.0, 3.0, 81, 86 });
            notes.push_back ({ outroBar * 4.0 + 6.0, 2.0, 74, 66 });
            return notes;
        }

        /** What gets played into the bass track. Pass 0 is the song; pass 1 goes over bars 12-16. */
        std::vector<Note> bassNotes (int pass)
        {
            std::vector<Note> notes;

            if (pass == 1)
            {
                for (int bar = chorusBar; bar < chorusBar + 4; ++bar)
                {
                    const int root = chordForBar (bar).bass;
                    for (int s = 0; s < 8; ++s)
                        notes.push_back ({ bar * 4.0 + s * 0.5, 0.3, root + (s % 2 ? 12 : 0), s % 2 ? 84 : 100 });
                }

                return notes;
            }

            for (int bar = 2; bar < outroBar; ++bar)
            {
                const double b = bar * 4.0;
                const int root = chordForBar (bar).bass;

                if (bar < verseBar || bar >= bridgeBar)
                {
                    notes.push_back ({ b, 1.8, root, 90 });
                    notes.push_back ({ b + 2.0, 1.8, root, 84 });
                }
                else if (bar < chorusBar)
                {
                    for (int e = 0; e < 8; ++e)
                        notes.push_back ({ b + e * 0.5, 0.42, root, e % 2 ? 78 : 96 });
                }
                else
                {
                    notes.push_back ({ b,        0.7,  root,      104 });
                    notes.push_back ({ b + 1.0,  0.45, root,      86 });
                    notes.push_back ({ b + 1.5,  0.45, root + 12, 92 });
                    notes.push_back ({ b + 2.5,  0.45, root,      96 });
                    notes.push_back ({ b + 3.0,  0.45, root,      92 });
                    notes.push_back ({ b + 3.5,  0.45, root + 12, 88 });
                }
            }

            return notes;
        }

        //==============================================================================
        // What gets played into the drum tracks: three passes that are clearly different.

        struct DrumPass { Mono kick, snare, hats, room; };

        Mono foldDown (const juce::AudioBuffer<float>& stereo)
        {
            Mono mono (1, stereo.getNumSamples());
            mono.clear();
            mono.addFrom (0, 0, stereo, 0, 0, stereo.getNumSamples(), 0.5f);
            mono.addFrom (0, 0, stereo, 1, 0, stereo.getNumSamples(), 0.5f);
            return mono;
        }

        DrumPass playDrums (int pass)
        {
            const int length = (int) (barToSeconds (lengthInBars) * rate);
            DrumKit kit (length, beatToSeconds, rate, 2026 + pass * 100);

            for (int bar = 0; bar < endBar; ++bar)
            {
                const double b = bar * 4.0;
                const bool intro = bar < verseBar, verse = bar >= verseBar && bar < chorusBar;
                const bool chorus = bar >= chorusBar && bar < bridgeBar, bridge = bar >= bridgeBar && bar < outroBar;

                if (bar == outroBar)
                {
                    // One last hit: the outro slows down, and audio doesn't follow the tempo.
                    kit.addKick (b, 0.95f);
                    kit.addHat (b, 0.7f, true, 0.0f);
                }
                else if (bar > outroBar)
                {
                    continue;
                }
                else if (pass == 0)
                {
                    if (intro && bar >= 2)      kit.addKick (b, 0.8f);
                    if (verse)                  { kit.addKick (b, 0.95f); kit.addKick (b + 1.5, 0.75f); kit.addKick (b + 2.0, 0.9f); }
                    if (chorus)                 for (int k = 0; k < 4; ++k) kit.addKick (b + k, 0.95f);
                    if (verse || chorus)        { kit.addSnare (b + 1, 0.8f); kit.addSnare (b + 3, 0.85f); }

                    if (chorus)
                        for (int s = 0; s < 16; ++s)
                            kit.addHat (b + s * 0.25, s % 2 == 0 ? 0.55f : 0.3f, s % 4 == 2, s % 2 ? 0.2f : -0.2f);
                    else if (bar >= 1 && ! bridge)
                        for (int e = 0; e < 8; ++e)
                            kit.addHat (b + e * 0.5, e % 2 == 0 ? 0.42f : 0.26f, false, e % 2 ? 0.2f : -0.15f);
                    else if (bridge)
                        for (int q = 0; q < 4; ++q)
                            kit.addHat (b + q, 0.4f, false, 0.0f);
                }
                else if (pass == 1)
                {
                    // Busier: syncopated kick, ghost notes, open hats on the off-beats.
                    kit.addKick (b, 0.95f); kit.addKick (b + 0.75, 0.7f); kit.addKick (b + 2.5, 0.85f);
                    kit.addSnare (b + 1, 0.8f); kit.addSnare (b + 3, 0.85f); kit.addSnare (b + 3.75, 0.3f);

                    for (int e = 0; e < 8; ++e)
                        kit.addHat (b + e * 0.5, e % 2 ? 0.5f : 0.3f, e % 2 == 1, 0.1f);
                }
                else
                {
                    // Half-time: the snare on 3, a long kick pattern, sixteenths on the hats.
                    kit.addKick (b, 0.95f); kit.addKick (b + 1.75, 0.6f); kit.addKick (b + 2.75, 0.75f);
                    kit.addSnare (b + 2, 0.95f);

                    for (int s = 0; s < 16; ++s)
                        kit.addHat (b + s * 0.25, s % 4 == 0 ? 0.55f : 0.22f, false, -0.1f);
                }
            }

            DrumPass out { foldDown (kit.kick), foldDown (kit.snare), foldDown (kit.hats), silence() };

            // The room: everything, duller and a few reflections later.
            for (auto [delayMs, gain] : { std::pair { 11.0, 0.5f }, std::pair { 23.0, 0.35f }, std::pair { 41.0, 0.25f }, std::pair { 67.0, 0.18f } })
            {
                const int delay = (int) (delayMs * 0.001 * rate);
                for (auto* stem : { &out.kick, &out.snare, &out.hats })
                    out.room.addFrom (0, delay, *stem, 0, 0, length - delay, gain * (stem == &out.hats ? 0.3f : 1.0f));
            }

            float low = 0.0f;
            for (int i = 0; i < length; ++i)
            {
                low += 0.25f * (out.room.getSample (0, i) - low);
                out.room.setSample (0, i, low);
            }

            return out;
        }

        //==============================================================================
        // What gets played into the guitar track: plucked strings (Karplus-Strong), driven.

        void pluck (Mono& out, double startSeconds, double seconds, int note, float gain, juce::Random& random)
        {
            std::vector<float> line ((size_t) juce::roundToInt (rate / juce::MidiMessage::getMidiNoteInHertz (note)));

            for (auto& v : line)
                v = random.nextFloat() * 2.0f - 1.0f;

            const int start = (int) (startSeconds * rate), length = (int) (seconds * rate), fade = (int) (0.006 * rate);
            size_t pos = 0;
            float previous = 0.0f;

            for (int i = 0; i < length && start + i < out.getNumSamples(); ++i)
            {
                const auto v = line[pos];
                line[pos] = (v + previous) * 0.5f * 0.998f;
                previous = v;
                pos = (pos + 1) % line.size();
                const auto envelope = juce::jmin (1.0f, (float) (length - i) / (float) fade);
                out.addSample (0, start + i, v * gain * envelope);
            }
        }

        void drive (Mono& b)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.6f * std::tanh (2.5f * b.getSample (0, i)));
        }

        /** A rhythm for `style` over bars [from, to): 0 chugged eighths, 1 octaves, 5 ringing chords,
            the others an offbeat riff that's different each time. Beats count on past the song, for the loop. */
        void playGuitar (Mono& out, int style, int from, int to, int seed)
        {
            juce::Random random (seed);

            for (int bar = from; bar < to; ++bar)
            {
                const double b = bar * 4.0;
                const auto power = chordForBar (bar).power;

                if (style == 0)
                {
                    for (int e = 0; e < 8; ++e)
                        for (auto key : power)
                            pluck (out, beatToSeconds (b + e * 0.5), e % 4 == 0 ? 0.28 : 0.14, key, 0.5f, random);
                }
                else if (style == 1)
                {
                    for (int e = 0; e < 8; ++e)
                        pluck (out, beatToSeconds (b + e * 0.5), 0.26, power[0] + (e % 2 ? 12 : 0) + (e == 6 ? 7 : 0), 0.65f, random);
                }
                else if (style == 5)
                {
                    for (auto key : power)
                        pluck (out, beatToSeconds (b), 2.2, key + 12, 0.6f, random);
                }
                else
                {
                    static constexpr double hits[3][4] { { 0.5, 1.5, 2.5, 3.5 }, { 0.0, 0.75, 1.5, 3.0 }, { 0.25, 1.0, 2.25, 3.25 } };
                    for (auto hit : hits[style % 3])
                        for (auto key : power)
                            pluck (out, beatToSeconds (b + hit), 0.2, key + (style % 2 ? 12 : 0), 0.55f, random);
                }
            }

            drive (out);
        }

        //==============================================================================
        /** Plays parts into CDAW like an audio interface and a MIDI keyboard would: what's on each
            input follows the song position from the moment the transport starts. */
        class FakeInterface
        {
        public:
            static constexpr int numInputs = 6;

            FakeInterface (te::HostedAudioDeviceInterface& i) : io (i), thread ([this] { run(); }) {}

            ~FakeInterface()
            {
                stop = true;
                thread.join();
            }

            void setContext (te::TransportControl* t)                   { transport = t; }
            void setInput (int channel, const Mono* source)             { sources[(size_t) channel] = source; }
            void setNotes (const std::vector<Note>* notes)              { midiNotes = notes; }
            void clear()
            {
                for (auto& s : sources)
                    s = nullptr;
                midiNotes = nullptr;
            }

            /** Where the song is, not counting loops: -1 when it's not playing. */
            double getSongTime() const                                  { return songTime; }

        private:
            void run()
            {
                juce::AudioBuffer<float> block (numInputs, blockSize);
                juce::MidiBuffer midi;
                const auto blockSeconds = blockSize / rate;
                auto next = juce::Time::getMillisecondCounterHiRes();
                double now = -1.0;
                std::vector<std::pair<double, juce::MidiMessage>> events;
                size_t nextEvent = 0;

                while (! stop)
                {
                    block.clear();
                    midi.clear();

                    auto* t = transport.load();
                    auto* context = t != nullptr ? t->getCurrentPlaybackContext() : nullptr;

                    if (context != nullptr && context->isPlaying())
                    {
                        if (now < 0.0)
                        {
                            now = context->getUnloopedPosition().inSeconds();

                            // The keyboard's events from here on.
                            events.clear();
                            nextEvent = 0;

                            if (auto* notes = midiNotes.load())
                            {
                                for (auto& n : *notes)
                                {
                                    const auto on = beatToSeconds (n.beat);

                                    if (on >= now - 0.001)
                                    {
                                        events.push_back ({ on, juce::MidiMessage::noteOn (1, n.key, (juce::uint8) n.velocity) });
                                        events.push_back ({ beatToSeconds (n.beat + n.length), juce::MidiMessage::noteOff (1, n.key) });
                                    }
                                }

                                std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.first < b.first; });
                            }
                        }

                        const auto start = (int) std::round (now * rate);

                        for (int ch = 0; ch < numInputs; ++ch)
                            if (auto* source = sources[(size_t) ch].load())
                                if (start >= 0 && start + blockSize <= source->getNumSamples())
                                    block.copyFrom (ch, 0, *source, 0, start, blockSize);

                        for (; nextEvent < events.size() && events[nextEvent].first < now + blockSeconds; ++nextEvent)
                            midi.addEvent (events[nextEvent].second, juce::jlimit (0, blockSize - 1, (int) ((events[nextEvent].first - now) * rate)));

                        now += blockSeconds;
                        songTime = now;
                    }
                    else
                    {
                        now = -1.0;
                        songTime = -1.0;
                    }

                    io.processBlock (block, midi);

                    next += 1000.0 * blockSeconds / feedSpeed;
                    auto wait = next - juce::Time::getMillisecondCounterHiRes();

                    if (wait > 1.0)
                        std::this_thread::sleep_for (std::chrono::milliseconds ((int) wait));
                }
            }

            te::HostedAudioDeviceInterface& io;
            std::atomic<bool> stop { false };
            std::atomic<double> songTime { -1.0 };
            std::atomic<te::TransportControl*> transport { nullptr };
            std::array<std::atomic<const Mono*>, numInputs> sources {};
            std::atomic<const std::vector<Note>*> midiNotes { nullptr };
            std::thread thread;
        };

        //==============================================================================
        double correlation (const float* a, const float* b, int n)
        {
            double ab = 0.0, aa = 0.0, bb = 0.0;

            for (int i = 0; i < n; ++i)
            {
                ab += (double) a[i] * b[i];
                aa += (double) a[i] * a[i];
                bb += (double) b[i] * b[i];
            }

            if (aa < 1.0e-9 && bb < 1.0e-9)     return 1.0;     // silence on both sides
            if (aa < 1.0e-9 || bb < 1.0e-9)     return 0.0;

            return ab / std::sqrt (aa * bb);
        }

        struct Match { double correlation = 0.0; int lag = 0; };

        /** How well `rendered` follows `source` from `sourceStart`, allowing for a small fixed lag. */
        Match findMatch (const Mono& rendered, const Mono& source, int sourceStart)
        {
            const auto* r = rendered.getReadPointer (0);
            const auto* s = source.getReadPointer (0);
            const int n = rendered.getNumSamples(), window = juce::jmin (16000, n);

            // The lag from the loudest stretch: quiet ones don't say much.
            int loudest = 0;
            float bestEnergy = -1.0f;

            for (int w = 0; w + window <= n; w += juce::jmax (1, window / 2))
            {
                float energy = 0.0f;
                for (int i = 0; i < window; ++i)
                    energy += r[w + i] * r[w + i];

                if (energy > bestEnergy)
                {
                    bestEnergy = energy;
                    loudest = w;
                }
            }

            Match best;
            double bestCorrelation = -2.0;

            for (int lag = -1200; lag <= 1200; ++lag)
            {
                const int from = sourceStart + loudest + lag;

                if (from < 0 || from + window > source.getNumSamples())
                    continue;

                if (auto c = correlation (r + loudest, s + from, window); c > bestCorrelation)
                {
                    bestCorrelation = c;
                    best.lag = lag;
                }
            }

            const int from = juce::jmax (0, sourceStart + best.lag);
            best.correlation = correlation (r, s + from, juce::jmin (n, source.getNumSamples() - from));
            return best;
        }

        /** The clips of these tracks as they are, without plugins: the same every time. */
        Mono renderClips (te::Edit& edit, juce::Array<te::Track*> tracks, te::TimeRange range)
        {
            for (int i = tracks.size(); --i >= 0;)
                if (auto parent = tracks[i]->getParentFolderTrack())
                    tracks.addIfNotAlreadyThere (parent);

            juce::TemporaryFile temp (".wav");
            te::Renderer::Parameters params (edit);
            params.destFile = temp.getFile();
            params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
            params.bitDepth = 32;
            params.sampleRateForAudio = rate;
            params.blockSizeForAudio = 512;
            params.time = range;
            params.tracksToDo = te::toBitSet (tracks);
            params.usePlugins = false;
            params.useMasterPlugins = false;
            params.checkNodesForAudio = false;
            params.canRenderInMono = false;
            te::Renderer::renderToFile ("Demo", params);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            Mono buffer;

            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (temp.getFile())))
            {
                buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
            }

            return buffer;
        }

        /** The loudest difference between two renders, and how many seconds in it is. */
        float differenceDb (const Mono& a, const Mono& b, double* where = nullptr)
        {
            float worst = 0.0f;

            for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
            {
                for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
                {
                    if (auto d = std::abs (a.getSample (ch, i) - b.getSample (ch, i)); d > worst)
                    {
                        worst = d;
                        if (where != nullptr)
                            *where = i / rate;
                    }
                }
            }

            return juce::Decibels::gainToDecibels (worst, -150.0f);
        }

        /** Where everything is, to see what changed. */
        juce::String describeLayout (te::Edit& edit)
        {
            juce::String s;

            for (auto* t : te::getAudioTracks (edit))
            {
                s << t->getName() << ":";

                for (auto c : t->getClips())
                {
                    s << " " << juce::String (c->getPosition().getStart().inSeconds(), 3) << "-" << juce::String (c->getPosition().getEnd().inSeconds(), 3)
                      << "@" << juce::String (c->getPosition().getOffset().inSeconds(), 3) << "#" << CompOps::getActiveTake (*c);

                    if (c->isMuted())
                        s << "m";

                    if (auto* a = dynamic_cast<te::AudioClipBase*> (c))
                        s << "f" << juce::String (a->getFadeIn().inSeconds(), 3) << "/" << juce::String (a->getFadeOut().inSeconds(), 3);
                }

                s << "\n";
            }

            auto& ts = edit.tempoSequence;

            for (int i = 0; i < ts.getNumTempos(); ++i)
                s << "tempo " << juce::String (ts.getTempo (i)->getBpm(), 1) << " at beat " << juce::String (ts.getTempo (i)->getStartBeat().inBeats(), 3) << "\n";

            return s;
        }

        te::LoudnessMeter::Readings measure (const juce::AudioBuffer<float>& b, double sampleRate)
        {
            te::LoudnessMeter meter;
            meter.prepare (sampleRate, b.getNumChannels(), 4096);
            meter.process (b.getArrayOfReadPointers(), b.getNumChannels(), b.getNumSamples());
            meter.flush();
            return meter.getReadings();
        }

        te::Clip* clipAt (te::ClipTrack& t, double seconds)
        {
            for (auto c : t.getClips())
                if (c->getEditTimeRange().contains (te::TimePosition::fromSeconds (seconds)))
                    return c;

            return nullptr;
        }

        te::AudioTrack* findTrack (te::Edit& edit, const juce::String& name)
        {
            for (auto* t : te::getAudioTracks (edit))
                if (t->getName() == name)
                    return t;

            return nullptr;
        }

        te::FolderTrack* findFolder (te::Edit& edit, const juce::String& name)
        {
            for (auto* t : te::getAllTracks (edit))
                if (auto* ft = dynamic_cast<te::FolderTrack*> (t); ft != nullptr && ft->getName() == name)
                    return ft;

            return nullptr;
        }

        template <typename PluginType>
        PluginType* addPlugin (te::Track& track, int index)
        {
            auto plugin = track.edit.getPluginCache().createNewPlugin (PluginType::xmlTypeName, {});
            track.pluginList.insertPlugin (plugin, index, nullptr);
            return dynamic_cast<PluginType*> (plugin.get());
        }
    }

    //==============================================================================
    struct DemoProject : public juce::UnitTest
    {
        DemoProject() : juce::UnitTest ("Demo project", "CDAW") {}

        static constexpr const char* songName = "Harbour Lights";

        // What each part was played from, to compare what's on the tracks with.
        std::array<DrumPass, 3> drums;
        Mono guitarPass0 = silence(), guitarPass1 = silence(), guitarLoop = silence(), guitarPunch = silence();

        void runTest() override
        {
            auto target = juce::SystemStats::getEnvironmentVariable ("CDAW_MAKE_DEMO", {});
            if (target.isEmpty())
                return;

            auto parent = juce::File (target);
            parent.createDirectory();

            if (parent.getChildFile (songName).exists())
            {
                beginTest ("Build the demo project");
                logMessage ("\"" + juce::String (songName) + "\" is there already: left as it is");
                return;
            }

            CDAWLookAndFeel laf;
            juce::LookAndFeel::setDefaultLookAndFeel (&laf);

            auto editFile = ProjectSession::createProjectFolder (parent, songName);
            buildSong (editFile);

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEMO_ARM_WAIT_MS", {}).isEmpty())
                checkWhatComesOut (editFile);

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            logMessage ("Demo project: " + editFile.getFullPathName());
        }

        //==============================================================================
        void expectPlays (te::Edit& edit, te::Track& track, te::TimeRange range, const Mono& source, double sourceSeconds, const juce::String& what)
        {
            // Not the very edges: the joins have short fades.
            const auto edge = te::TimeDuration::fromSeconds (0.03);
            auto rendered = renderClips (edit, { &track }, range.reduced (edge));
            auto match = findMatch (rendered, source, (int) ((sourceSeconds + edge.inSeconds()) * rate));
            logMessage (what + " on " + track.getName() + ": correlation " + juce::String (match.correlation, 4) + ", " + juce::String (match.lag) + " samples off");
            expectGreaterThan (match.correlation, 0.98, what + " plays on " + track.getName());
        }

        /** How long after arming the first recording starts (CDAW_DEMO_ARM_WAIT_MS, to look for races). */
        static int armToRecordMs()
        {
            auto custom = juce::SystemStats::getEnvironmentVariable ("CDAW_DEMO_ARM_WAIT_MS", {});
            return custom.isNotEmpty() ? custom.getIntValue() : 500;
        }

        void saveSnapshot (juce::Component& c, const juce::String& name)
        {
            auto custom = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {});
            auto dir = custom.isNotEmpty() ? juce::File (custom) : getTestDir().getChildFile ("snapshots");
            dir.createDirectory();
            auto file = dir.getChildFile ("demo_" + name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), out);
        }

        //==============================================================================
        void buildSong (const juce::File& editFile)
        {
            auto& engine = getEngine();
            auto& dm = engine.getDeviceManager();

            beginTest ("Play the parts in");

            for (int pass = 0; pass < 3; ++pass)
                drums[(size_t) pass] = playDrums (pass);

            playGuitar (guitarPass0, 0, verseBar, bridgeBar, 11);
            playGuitar (guitarPass1, 1, verseBar, bridgeBar, 12);
            playGuitar (guitarPunch, 5, verseBar, bridgeBar, 13);

            // The loop goes round bars 12-16 four times: three whole passes and a bit.
            {
                for (int pass = 0; pass < 4; ++pass)
                {
                    Mono one = silence();
                    playGuitar (one, 2 + pass, chorusBar + pass * 4, chorusBar + (pass + 1) * 4, 20 + pass);
                    guitarLoop.addFrom (0, 0, one, 0, 0, one.getNumSamples());
                }
            }

            te::HostedAudioDeviceInterface::Parameters params;
            params.sampleRate = rate;
            params.blockSize = blockSize;
            params.inputChannels = FakeInterface::numInputs;
            params.outputChannels = 2;

            for (int i = 0; i < FakeInterface::numInputs; ++i)
                params.inputNames.add ("In " + juce::String (i + 1));

            auto& io = dm.getHostedAudioDeviceInterface();
            io.initialise (params);
            io.prepareToPlay (params.sampleRate, params.blockSize);
            dm.dispatchPendingUpdates();
            dm.setAllWaveInputsToNumChannels (1);
            dm.dispatchPendingUpdates();
            dm.rescanMidiDeviceList();

            for (int tries = 0; tries < 100 && dm.getMidiInDevices().empty(); ++tries)
            {
                dm.dispatchPendingUpdates();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            }

            // What CDAWEngine::applyRecordingDefaults sets, with its defaults.
            const auto replaceMode = te::WaveInputDevice::getMergeModes()[1];

            for (auto* in : dm.getWaveInputDevices())
                if (! in->isTrackDevice())
                    in->setMergeMode (replaceMode);

            for (auto& in : dm.getMidiInDevices())
            {
                in->setEnabled (true);
                in->mergeRecordings = false;
                in->replaceExistingClips = false;
            }

            dm.dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            ProjectSession session (engine, editFile);
            auto& edit = session.getEdit();
            auto& vs = session.getViewState();
            auto& sm = session.getSelectionManager();
            auto& ts = edit.tempoSequence;
            auto& transport = session.getTransport();
            ts.getTempo (0)->setBpm (bpm);
            transport.ensureContextAllocated();

            auto barTime = [&] (double bar) { return ts.toTime (te::BeatPosition::fromBeats (bar * 4.0)); };
            auto bars = [&] (double from, double to) { return te::TimeRange (barTime (from), barTime (to)); };

            {
                juce::Array<te::Track*> old;
                for (auto t : te::getAudioTracks (edit)) old.add (t);
                TrackOps::deleteTracks (edit, old);
            }

            // Set up as for a session: the kit in a group with group editing on, the inputs assigned.
            auto drumGroup = TrackOps::addFolderTrack (edit, nullptr, true);
            drumGroup->setName ("Drums");
            drumGroup->setColour (juce::Colour (0xffe5383b));
            GroupEdit::setEnabled (*drumGroup, true);

            std::array<te::AudioTrack*, 4> kit {};
            {
                te::Track* previous = nullptr;
                const std::pair<const char*, juce::Colour> pieces[] { { "Kick", juce::Colour (0xffe5484d) }, { "Snare", juce::Colour (0xffff6f61) },
                                                                      { "Hats", juce::Colour (0xffff9a8a) }, { "Room", juce::Colour (0xffc44536) } };

                for (int i = 0; i < 4; ++i)
                {
                    auto t = TrackOps::addAudioTrack (edit, nullptr, nullptr, pieces[i].first);
                    t->setColour (pieces[i].second);
                    edit.moveTrack (t, te::TrackInsertPoint (drumGroup.get(), previous));
                    TrackOps::assignAudioInput (*t, i, 1);
                    kit[(size_t) i] = t.get();
                    previous = t.get();
                }
            }

            auto guitar = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Guitar");
            guitar->setColour (juce::Colour (0xff4fd1c5));
            TrackOps::assignAudioInput (*guitar, 4, 1);

            const auto fourOsc = te::PluginManager::createBuiltInPluginDescription<te::FourOscPlugin> (true);
            auto bass = TrackOps::addInstrumentTrack (edit, nullptr, &fourOsc, nullptr, false);
            bass->setName ("Bass");
            bass->setColour (juce::Colour (0xff4fa3ff));
            soundBass (*addSynth (*bass));

            auto midiInputs = TrackOps::getInputChoices (edit, true);
            expect (! midiInputs.isEmpty(), "The fake interface has a MIDI input");

            if (! midiInputs.isEmpty())
                TrackOps::assignInput (*bass, midiInputs[0].instance);

            // The arrangement stays open while recording: take lanes open by themselves.
            ArrangeView arrange (session, vs);
            arrange.setBounds (0, 0, 1500, 900);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            {
                FakeInterface input (io);
                input.setContext (&transport);

                auto record = [&] (double fromBar, double toBar, double untilSongBar)
                {
                    juce::ignoreUnused (toBar);
                    transport.setPosition (barTime (fromBar));
                    transport.record (false);

                    const auto until = barToSeconds (untilSongBar);
                    const auto giveUp = juce::Time::getMillisecondCounterHiRes() + 300000.0;

                    while (input.getSongTime() < until && juce::Time::getMillisecondCounterHiRes() < giveUp)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

                    expect (input.getSongTime() >= until, "The recording got to where it should");
                    transport.stop (false, false);

                    // The transport finishes stopping, and the clips land.
                    for (int i = 0; i < 40 && input.getSongTime() >= 0.0; ++i)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

                    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                    input.clear();
                };

                auto armKit = [&] (bool armed)
                {
                    for (auto* t : kit)
                        TrackOps::setArmed (*t, armed);
                };

                auto playIntoKit = [&] (int pass)
                {
                    const auto& p = drums[(size_t) pass];
                    input.setInput (0, &p.kick);
                    input.setInput (1, &p.snare);
                    input.setInput (2, &p.hats);
                    input.setInput (3, &p.room);
                };

                beginTest ("Record the kit three times, on four tracks at once");
                armKit (true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (armToRecordMs());
                playIntoKit (0);
                record (0, endBar, endBar);
                playIntoKit (1);
                record (8, 20, 20);
                playIntoKit (2);
                record (14, 18, 18);
                armKit (false);

                for (auto* t : kit)
                {
                    expectEquals (t->getClips().size(), 5, "Pass 1, 2, 3, 2, 1 along " + t->getName());
                    expectEquals (CompOps::getMaxTakesOnTrack (*t), 3);
                }

                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                expect (EditViewState::isShowingTakes (*kit[0]), "The first track of the group opens its lanes");
                expect (! EditViewState::isShowingTakes (*kit[1]), "The others in the group don't");

                beginTest ("Record the guitar over itself, in a loop, and punched in");
                TrackOps::setArmed (*guitar, true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (armToRecordMs());
                input.setInput (4, &guitarPass0);
                record (verseBar, bridgeBar, bridgeBar);
                expectEquals (guitar->getClips().size(), 1);

                input.setInput (4, &guitarPass1);
                record (8, 12, 12);
                expectEquals (guitar->getClips().size(), 3, "Pass 1 either side of pass 2");
                expectEquals (CompOps::getActiveTake (*clipAt (*guitar, barToSeconds (9))), 1);
                expect (EditViewState::isShowingTakes (*guitar), "Its lanes open after the first record-over");

                input.setInput (4, &guitarLoop);
                transport.setLoopRange (bars (12, 16));
                transport.looping = true;
                record (12, 16, 26);
                transport.looping = false;

                auto takesAfterLoop = CompOps::getMaxTakesOnTrack (*guitar);
                expectEquals (takesAfterLoop, 6, "Passes 1 and 2, three whole loops and a part of one");
                auto* looped = clipAt (*guitar, barToSeconds (13));
                expect (looped != nullptr);
                expectEquals (CompOps::getActiveTake (*looped), 4, "The last whole pass plays");

                input.setInput (4, &guitarPunch);
                edit.recordingPunchInOut = true;
                transport.setLoopRange (bars (18, 19));
                record (17, 19, 20);
                edit.recordingPunchInOut = false;
                transport.setLoopRange ({});

                auto* punched = clipAt (*guitar, barToSeconds (18.5));
                expect (punched != nullptr);
                expectEquals (CompOps::getActiveTake (*punched), takesAfterLoop, "The punch plays in its range");
                expectWithinAbsoluteError (punched->getPosition().getStart().inSeconds(), barToSeconds (18), 0.03, "The punch starts where it was set");
                expectWithinAbsoluteError (punched->getPosition().getEnd().inSeconds(), barToSeconds (19), 0.03, "The punch ends where it was set");
                expect (CompOps::getActiveTake (*clipAt (*guitar, barToSeconds (17.5))) != takesAfterLoop, "Before the punch: what was there");
                expect (CompOps::getActiveTake (*clipAt (*guitar, barToSeconds (19.5))) != takesAfterLoop, "After the punch: what was there");
                TrackOps::setArmed (*guitar, false);

                beginTest ("Record the bass, then over part of it");
                TrackOps::setArmed (*bass, true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (armToRecordMs());
                const auto bassPass0 = bassNotes (0), bassPass1 = bassNotes (1);
                input.setNotes (&bassPass0);
                record (1.75, outroBar, outroBar);
                expectEquals (bass->getClips().size(), 1);

                // Started a moment before the first note, as a player does: a note exactly on the
                // first block is before the recording's first callback.
                input.setNotes (&bassPass1);
                record (11.75, 16, 16);
                TrackOps::setArmed (*bass, false);
                expectEquals (bass->getClips().size(), 3);
                expectEquals (CompOps::getMaxTakesOnTrack (*bass), 2);
                expect (EditViewState::isShowingTakes (*bass), "Its lanes open too");
                checkBassTake (*bass, bassPass0, bassPass1);

                for (auto* in : dm.getWaveInputDevices())
                    if (! in->isTrackDevice())
                        expect (in->getMergeMode() == replaceMode, "Each input is back on replace after recording");
            }

            beginTest ("What was recorded is what was played");
            {
                const auto kitParts = [this] (int pass, int part) -> const Mono&
                {
                    auto& p = drums[(size_t) pass];
                    return part == 0 ? p.kick : part == 1 ? p.snare : part == 2 ? p.hats : p.room;
                };

                for (int part = 0; part < 4; ++part)
                {
                    expectPlays (edit, *kit[(size_t) part], bars (0, 8), kitParts (0, part), 0.0, "Pass 1");
                    expectPlays (edit, *kit[(size_t) part], bars (8, 14), kitParts (1, part), barToSeconds (8), "Pass 2");
                    expectPlays (edit, *kit[(size_t) part], bars (14, 18), kitParts (2, part), barToSeconds (14), "Pass 3");
                }

                expectPlays (edit, *guitar, bars (4, 8), guitarPass0, barToSeconds (4), "Guitar pass 1");
                expectPlays (edit, *guitar, bars (8, 12), guitarPass1, barToSeconds (8), "Guitar pass 2");
                expectPlays (edit, *guitar, bars (12, 16), guitarLoop, barToSeconds (12 + 8), "Guitar loop pass 3");
                expectPlays (edit, *guitar, bars (18, 19), guitarPunch, barToSeconds (18), "Guitar punch");
            }

            // Looking for how soon after arming a recording can start: nothing else matters then.
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEMO_ARM_WAIT_MS", {}).isNotEmpty())
                return;

            beginTest ("Comp through the group, then mute the bridge's drums");
            {
                auto& um = edit.getUndoManager();
                um.beginNewTransaction ("Comp");
                GroupEdit::compRange (*kit[0], bars (10, 12), 0);

                for (auto* t : kit)
                    expectEquals (t->getClips().size(), 7, "The swipe cuts every drum track");

                for (int part = 0; part < 4; ++part)
                {
                    const auto& p = part == 0 ? drums[0].kick : part == 1 ? drums[0].snare : part == 2 ? drums[0].hats : drums[0].room;
                    expectPlays (edit, *kit[(size_t) part], bars (10, 12), p, barToSeconds (10), "Comped back to pass 1");
                }

                um.beginNewTransaction ("Mute the bridge");
                GroupEdit::splitAt (*kit[1], barTime (outroBar));

                auto* bridgeClip = clipAt (*kit[2], barToSeconds (bridgeBar + 1));
                expect (bridgeClip != nullptr, "The hats were recorded through the bridge");

                if (bridgeClip != nullptr)
                {
                    GroupEdit::selectClip (sm, *bridgeClip, false);
                    expectEquals (ClipOps::getSelectedClips (sm).size(), 4, "A click on one selects the lined-up clips");
                    ClipOps::setMuted (ClipOps::getSelectedClips (sm), true);
                    sm.deselectAll();
                }

                for (auto* t : kit)
                    if (auto* c = clipAt (*t, barToSeconds (bridgeBar + 1)))
                        expect (c->isMuted());

                // The guitar: a bit of the first loop pass in the middle of the last one.
                um.beginNewTransaction ("Comp the guitar");
                CompOps::compRange (*guitar, bars (14, 15), 2);
                expectPlays (edit, *guitar, bars (14, 15), guitarLoop, barToSeconds (14), "Guitar loop pass 1");
                expectPlays (edit, *guitar, bars (12, 14), guitarLoop, barToSeconds (20), "Guitar loop pass 3");
                expectPlays (edit, *guitar, bars (15, 16), guitarLoop, barToSeconds (23), "Guitar loop pass 3");
            }

            // Pad and lead, the rest of the song.
            auto drawNotes = [&] (te::AudioTrack& t, const std::vector<Note>& notes, const juce::String& name)
            {
                auto clip = t.insertMIDIClip (bars (0, endBar + 2), nullptr);
                clip->setName (name);
                for (auto& n : notes)
                    clip->getSequence().addNote (n.key, te::BeatPosition::fromBeats (n.beat), te::BeatDuration::fromBeats (n.length), n.velocity, 0, nullptr);
            };

            auto padDescription = BuiltInPlugins::getSynthDescription(), leadDescription = BuiltInPlugins::getLeadDescription();
            auto pad = TrackOps::addInstrumentTrack (edit, nullptr, &padDescription, nullptr, false);
            pad->setName ("Pad");
            pad->setColour (juce::Colour (0xffd77cff));
            auto lead = TrackOps::addInstrumentTrack (edit, nullptr, &leadDescription, nullptr, false);
            lead->setName ("Lead");
            lead->setColour (juce::Colour (0xfff2b134));

            auto firstPlugin = [] (te::AudioTrack& t) { return t.pluginList.getPlugins().getFirst().get(); };
            auto* padSynth = dynamic_cast<SynthPlugin*> (firstPlugin (*pad));
            auto* leadSynth = dynamic_cast<LeadPlugin*> (firstPlugin (*lead));
            expect (padSynth != nullptr && leadSynth != nullptr, "C-Synth and C-Lead load");

            if (padSynth == nullptr || leadSynth == nullptr)
                return;

            soundPad (*padSynth);
            soundLead (*leadSynth);
            drawNotes (*pad, padNotes(), "Chords");
            drawNotes (*lead, leadNotes(), "Lead");

            beginTest ("Song sections, lyrics, notes, key and markers");
            {
                ArrangerOps::addSection (edit, bars (0, verseBar), "Intro");
                ArrangerOps::addSection (edit, bars (verseBar, chorusBar), "Verse");
                ArrangerOps::addSection (edit, bars (chorusBar, bridgeBar), "Chorus");
                ArrangerOps::addSection (edit, bars (bridgeBar, outroBar), "Bridge");
                ArrangerOps::addSection (edit, bars (outroBar, endBar), "Outro");

                auto sections = ArrangerOps::getSections (edit);
                SongNotes::setLyrics (*sections[1], "The ferries come in slow tonight\nthe water holds the amber light\n"
                                                    "I count the cranes along the quay\nand you were always counting me", nullptr);
                SongNotes::setLyrics (*sections[2], "Harbour lights, harbour lights\nbring me home across the bay\n"
                                                    "Harbour lights, harbour lights\ndon't you fade away", nullptr);
                SongNotes::setLyrics (*sections[3], "And if the tide won't turn for us\nI'll wait here till it does", nullptr);
                SongNotes::setProjectNotes (edit, "100 BPM, D minor, slowing to 84 for the outro.\n\n"
                                                  "Built by the demo generator to try out CDAW: drums, guitar and bass are real recordings "
                                                  "(with takes on their lanes), comped with group editing on the drums.\n\n"
                                                  "To do\n- harmony vocal in chorus 2\n- try guitar loop pass 2 in the chorus");
                KeyScale::setKey (edit, { 2, "Minor" }, nullptr);
                expectEquals (KeyScale::getKey (edit).getName(), juce::String ("D Minor"));
                TempoOps::addMarker (edit, barTime (chorusBar), "First chorus");
            }

            beginTest ("Copy the chorus: takes and lyrics come along");
            {
                auto takesBefore = CompOps::getMaxTakesOnTrack (*kit[0]);
                ArrangerOps::copyRange (edit, bars (chorusBar, bridgeBar), barTime (outroBar));

                auto sections = ArrangerOps::getSections (edit);
                expectEquals (sections.size(), 6);
                expectEquals (sections[4]->getName(), juce::String ("Chorus"));
                expectEquals (SongNotes::getLyrics (*sections[4]), SongNotes::getLyrics (*sections[2]), "The copy has the lyrics");
                expectEquals (CompOps::getMaxTakesOnTrack (*kit[0]), takesBefore);
                expect (CompOps::hasTakes (*clipAt (*kit[0], barToSeconds (outroBar + 1))), "A copied piece keeps its takes");
                expect (CompOps::hasTakes (*clipAt (*bass, barToSeconds (outroBar + 1))), "A copied MIDI piece keeps its takes");

                // The copy plays what the original does.
                // (Not its very edges, where the copy has its own short fades.)
                const auto edge = te::TimeDuration::fromSeconds (0.03);
                auto original = renderClips (edit, { kit[0], kit[1], kit[2], kit[3], guitar.get() }, bars (chorusBar, bridgeBar).reduced (edge));
                auto copy = renderClips (edit, { kit[0], kit[1], kit[2], kit[3], guitar.get() }, bars (outroBar, outroBar + 8).reduced (edge));
                double where = 0.0;
                auto difference = differenceDb (original, copy, &where);
                logMessage ("Chorus 1 against its copy: difference " + juce::String (difference, 1) + " dB, " + juce::String (where, 3) + " s in");
                expectLessThan (difference, -60.0f, "The copy of the chorus sounds the same");
            }

            const int outro2 = outroBar + 8;

            beginTest ("Effects, sends and automation");
            {
                // FX returns.
                auto reverbTrack = TrackOps::addFxTrack (edit, nullptr);
                reverbTrack->setName ("Reverb");
                auto* reverb = addPlugin<ReverbPlugin> (*reverbTrack, 0);
                reverb->mix.setParameter (1.0f, juce::sendNotification);
                reverb->decay.setParameter (2.6f, juce::sendNotification);
                reverb->preDelay.setParameter (24.0f, juce::sendNotification);
                reverb->size.setParameter (0.7f, juce::sendNotification);
                reverb->postLowCut.setParameter (220.0f, juce::sendNotification);
                reverb->postHighCut.setParameter (9000.0f, juce::sendNotification);
                const int reverbBus = MixerOps::getFxBuses (edit).getFirst().number;

                auto delayTrack = TrackOps::addFxTrack (edit, nullptr);
                delayTrack->setName ("Delay");
                auto* delay = addPlugin<DelayPlugin> (*delayTrack, 0);
                delay->parameter (DelayParameters::mix).setParameter (1.0f, juce::sendNotification);
                int delayBus = -1;

                for (auto& b : MixerOps::getFxBuses (edit))
                    if (b.number != reverbBus)
                        delayBus = b.number;

                expect (delayBus >= 0, "Two FX buses");

                MixerOps::addSend (*kit[1], reverbBus, true, -12.0f);
                MixerOps::addSend (*guitar, reverbBus, true, -14.0f);
                MixerOps::addSend (*guitar, delayBus, false, -18.0f);    // pre-fader
                MixerOps::addSend (*pad, reverbBus, true, -8.0f);
                MixerOps::addSend (*lead, reverbBus, true, -12.0f);
                MixerOps::addSend (*lead, delayBus, true, -16.0f);

                // Inserts: most of what ships.
                auto* glue = addPlugin<CompressorPlugin> (*drumGroup, 0);
                glue->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-16.0f), juce::sendNotification);
                glue->ratio.setParameter (1.0f / 3.0f, juce::sendNotification);
                glue->attackMs.setParameter (12.0f, juce::sendNotification);
                glue->releaseMs.setParameter (120.0f, juce::sendNotification);

                addPlugin<EsserPlugin> (*kit[2], 0);

                auto* guitarEq = addPlugin<EqPlugin> (*guitar, 0);
                guitarEq->setType (0, EqMath::BandType::lowCut);
                guitarEq->getBand (0).freq.setParameter (110.0f, juce::sendNotification);
                guitarEq->setOn (0, true);
                addPlugin<DistPlugin> (*guitar, 1);
                addPlugin<ChorusPlugin> (*guitar, 2);

                auto* bassEq = addPlugin<EqPlugin> (*bass, 1);
                bassEq->setType (0, EqMath::BandType::lowCut);
                bassEq->getBand (0).freq.setParameter (32.0f, juce::sendNotification);
                bassEq->setOn (0, true);

                auto* padEq = addPlugin<EqPlugin> (*pad, 1);
                padEq->setType (0, EqMath::BandType::lowCut);
                padEq->getBand (0).freq.setParameter (180.0f, juce::sendNotification);
                padEq->setOn (0, true);
                addPlugin<PhaserPlugin> (*pad, 2);

                auto* leadEq = addPlugin<EqPlugin> (*lead, 1);
                leadEq->setType (0, EqMath::BandType::lowCut);
                leadEq->getBand (0).freq.setParameter (220.0f, juce::sendNotification);
                leadEq->setOn (0, true);

                auto* masterEq = addPlugin<EqPlugin> (*edit.getMasterTrack(), 0);
                masterEq->setType (6, EqMath::BandType::highShelf);
                masterEq->getBand (6).freq.setParameter (10000.0f, juce::sendNotification);
                masterEq->getBand (6).gain.setParameter (1.5f, juce::sendNotification);
                masterEq->setOn (6, true);

                auto masterComp = edit.getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});
                edit.getMasterPluginList().insertPlugin (masterComp, 1, nullptr);
                auto* c = dynamic_cast<CompressorPlugin*> (masterComp.get());
                c->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-10.0f), juce::sendNotification);
                c->ratio.setParameter (0.5f, juce::sendNotification);
                c->attackMs.setParameter (30.0f, juce::sendNotification);
                c->releaseMs.setParameter (200.0f, juce::sendNotification);

                // The pad opens up through the intro and closes in the outro.
                const float open = padSynth->getSettings().cutoff;
                auto& curve = padSynth->parameter (SynthParameters::cutoff).getParameter().getCurve();
                curve.addPoint (barTime (0), open * std::pow (2.0f, -26.0f / 12.0f), 0.0f, nullptr);
                curve.addPoint (barTime (verseBar), open, 0.0f, nullptr);
                curve.addPoint (barTime (outro2), open, 0.0f, nullptr);
                curve.addPoint (barTime (outro2 + 4), open * std::pow (2.0f, -28.0f / 12.0f), 0.0f, nullptr);
                AutomationOps::showLane (*pad, padSynth->parameter (SynthParameters::cutoff).getParameter());

                // The mix: each part on its own to a target level.
                struct Target { te::AudioTrack* track; float db; bool byPeak; };
                const Target targets[] { { kit[0], -6.0f, true }, { kit[1], -9.0f, true }, { kit[2], -17.0f, true }, { kit[3], -16.0f, true },
                                         { guitar.get(), -22.0f, false }, { bass.get(), -19.0f, false },
                                         { pad.get(), -25.0f, false }, { lead.get(), -21.0f, false } };
                const auto chorusFrom = barTime (chorusBar).inSeconds(), chorusTo = barTime (bridgeBar).inSeconds();

                for (auto& [track, targetDb, byPeak] : targets)
                {
                    auto alone = render (edit, { track });
                    auto level = byPeak ? peakDb (alone) : rmsDb (alone, chorusFrom, chorusTo, rate);
                    auto fader = juce::jlimit (-30.0f, 6.0f, targetDb - level);
                    track->getVolumePlugin()->setVolumeDb (fader);
                    logMessage (track->getName() + ": " + juce::String (level, 1) + (byPeak ? " dB peak" : " dB RMS") + " alone, fader " + juce::String (fader, 1) + " dB");
                }

                kit[2]->getVolumePlugin()->setPan (0.12f);
                kit[3]->getVolumePlugin()->setPan (-0.08f);
                guitar->getVolumePlugin()->setPan (-0.35f);
                lead->getVolumePlugin()->setPan (0.1f);

                // The guitar steps back in the verse: a ride on its fader.
                {
                    auto faderNow = guitar->getVolumePlugin()->getVolumeDb();
                    auto& ride = guitar->getVolumePlugin()->volParam->getCurve();
                    ride.addPoint (barTime (verseBar), te::decibelsToVolumeFaderPosition (faderNow - 5.0f), 0.0f, nullptr);
                    ride.addPoint (barTime (chorusBar - 1), te::decibelsToVolumeFaderPosition (faderNow - 5.0f), 0.0f, nullptr);
                    ride.addPoint (barTime (chorusBar), te::decibelsToVolumeFaderPosition (faderNow), 0.0f, nullptr);
                    AutomationOps::showLane (*guitar, *guitar->getVolumePlugin()->volParam);
                }
            }

            beginTest ("A slower outro: MIDI follows the tempo, audio keeps its place");
            {
                auto kitClip = clipAt (*kit[0], barToSeconds (outro2 + 0.5));
                expect (kitClip != nullptr);
                auto kitStart = kitClip->getPosition().getStart();

                TempoOps::setTempoAt (edit, barTime (outro2), 84.0);
                expectEquals (ts.getNumTempos(), 2);
                expectWithinAbsoluteError (kitClip->getPosition().getStart().inSeconds(), kitStart.inSeconds(), 1.0e-6, "Audio stays");

                // The outro's clips still reach their last notes, now the notes are further apart.
                for (auto* t : { pad.get(), lead.get() })
                {
                    te::MidiClip* last = nullptr;

                    for (auto c : t->getClips())
                        if (auto mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr
                             && (last == nullptr || mc->getPosition().getEnd() > last->getPosition().getEnd()))
                            last = mc;

                    expect (last != nullptr);
                    if (last == nullptr)
                        continue;

                    // The song's last note: the piece holds all of them, this is the latest it should play.
                    const auto lastBeat = (outro2 + 4) * 4.0;
                    const auto wanted = ts.toTime (te::BeatPosition::fromBeats (lastBeat));
                    logMessage (t->getName() + ": the song's end at " + juce::String (wanted.inSeconds(), 2) + " s, the last clip to "
                                + juce::String (last->getPosition().getEnd().inSeconds(), 2) + " s");
                    expectGreaterThan (last->getPosition().getEnd().inSeconds(), wanted.inSeconds() - 0.01, "The clip still reaches the end of the song on " + t->getName());
                }
            }

            beginTest ("The master level");
            {
                auto master = edit.getMasterVolumePlugin();
                auto mix = render (edit);
                auto chorusFrom = barTime (chorusBar).inSeconds(), chorusTo = barTime (bridgeBar).inSeconds();
                auto gain = juce::jlimit (-24.0f, 6.0f, -13.0f - rmsDb (mix, chorusFrom, chorusTo, rate));

                for (int attempt = 0; attempt < 6; ++attempt)
                {
                    master->setVolumeDb (gain);
                    mix = render (edit);
                    auto peak = peakDb (mix);
                    logMessage ("Master " + juce::String (gain, 1) + " dB: chorus " + juce::String (rmsDb (mix, chorusFrom, chorusTo, rate), 1)
                                + " dB RMS, peak " + juce::String (peak, 1) + " dBFS");
                    if (peak < -2.0f)
                        break;
                    gain -= peak + 2.2f;
                }

                // A fade on the very end.
                auto& curve = master->volParam->getCurve();
                curve.addPoint (barTime (outro2 + 3), te::decibelsToVolumeFaderPosition (gain), 0.0f, nullptr);
                curve.addPoint (barTime (outro2 + 6), te::decibelsToVolumeFaderPosition (-40.0f), 0.0f, nullptr);
            }

            expect (session.save(), "Project saved");
            expect (MissingMedia::findMissingAudio (edit).isEmpty(), "Nothing missing");

            beginTest ("How it all looks");
            {
                for (auto* t : { (te::AudioTrack*) guitar.get(), (te::AudioTrack*) bass.get(), kit[0] })
                    EditViewState::setShowingTakes (*t, true);

                vs.pixelsPerSecond = 16.0;
                arrange.setBounds (0, 0, 1600, 1000);
                arrange.resized();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
                saveSnapshot (arrange, "arrange");

                if (auto* mc = dynamic_cast<te::MidiClip*> (clipAt (*bass, barToSeconds (13))))
                {
                    PianoRoll roll (vs, mc);
                    roll.setBounds (0, 0, 1200, 420);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    saveSnapshot (roll, "bass_take");
                }

                {
                    MixerView mixer (vs);
                    mixer.setBounds (0, 0, 1300, 460);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    saveSnapshot (mixer, "mixer");
                }

                {
                    edit.getTransport().setPosition (barTime (chorusBar + 1));
                    NotesPage notes (vs);
                    notes.setBounds (0, 0, 1200, 420);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
                    saveSnapshot (notes, "notes");
                    edit.getTransport().setPosition ({});
                }

                for (bool stems : { false, true })
                {
                    ExportDialog dialog (session, stems);
                    dialog.setSize (560, 560);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                    saveSnapshot (dialog, stems ? "export_stems" : "export_mix");
                }
            }

            exportBounces (session);

            // It opens at the start of the song, showing all of it.
            transport.setPosition ({});
            vs.viewStart = 0.0;
            vs.scrollY = 0;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            logMessage ("Saved with the playhead at " + juce::String (transport.getPosition().inSeconds(), 3) + " s");
            expect (session.save(), "Project saved");
        }

        void checkBassTake (te::AudioTrack& bass, const std::vector<Note>& pass0, const std::vector<Note>& pass1)
        {
            auto check = [&] (te::MidiClip& clip, const std::vector<Note>& played, double fromBar, double toBar, const juce::String& what)
            {
                auto& notes = clip.getSequence().getNotes();
                int expected = 0, found = 0;
                double worst = 0.0;

                for (auto& p : played)
                {
                    const auto when = beatToSeconds (p.beat);

                    if (when < barToSeconds (fromBar) || when >= barToSeconds (toBar) - 0.001)
                        continue;

                    ++expected;
                    double nearest = 1.0e9;

                    for (auto n : notes)
                        if (n->getNoteNumber() == p.key)
                            nearest = juce::jmin (nearest, std::abs (clip.getTimeOfContentBeat (n->getStartBeat()).inSeconds() - when));

                    if (nearest < 0.03)
                        ++found;

                    worst = juce::jmax (worst, nearest);
                }

                logMessage (what + ": " + juce::String (found) + " of " + juce::String (expected) + " notes in place, worst " + juce::String (worst * 1000.0, 1) + " ms off");
                expectEquals (found, expected, what + ": every note played is there");
            };

            if (auto* first = dynamic_cast<te::MidiClip*> (clipAt (bass, barToSeconds (5))))
                check (*first, pass0, 2, 12, "Bass pass 1");

            if (auto* second = dynamic_cast<te::MidiClip*> (clipAt (bass, barToSeconds (13))))
                check (*second, pass1, 12, 16, "Bass pass 2");
        }

        //==============================================================================
        static bool runJob (std::vector<ExportService::Job::Item> items, juce::StringArray& results)
        {
            std::atomic<bool> done { false };
            bool success = false;

            ExportService::Job job (std::move (items), [&] (bool ok, juce::StringArray r)
            {
                success = ok;
                results = r;
                done = true;
            });

            te::test_utilities::runDispatchLoopUntilTrue (done);
            return success;
        }

        juce::File exportTo (te::Edit& edit, const ExportService::Options& o, const juce::File& dest, const juce::Array<te::Track*>& tracks = {})
        {
            juce::StringArray results;
            expect (runJob ({ { ExportService::makeParameters (edit, o, dest, tracks), dest.getFileName() } }, results),
                    dest.getFileName() + ": " + results.joinIntoString ("; "));
            expect (dest.existsAsFile(), dest.getFileName() + " written");
            return dest;
        }

        juce::AudioBuffer<float> readBack (const juce::File& file, double* sampleRate = nullptr, int* bits = nullptr)
        {
            juce::AudioBuffer<float> buffer;

            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (getEngine().getAudioFileFormatManager().readFormatManager.createReaderFor (file)))
            {
                buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                if (sampleRate != nullptr)  *sampleRate = reader->sampleRate;
                if (bits != nullptr)        *bits = (int) reader->bitsPerSample;
            }
            else
            {
                expect (false, "Couldn't read " + file.getFileName());
            }

            return buffer;
        }

        /** What the Export dialog lists for "Stems: all tracks". */
        static juce::Array<te::Track*> stemTracks (te::Edit& edit)
        {
            juce::Array<te::Track*> tracks;

            for (auto* t : te::getAudioTracks (edit))
                if (! t->isMuted (true))
                    tracks.add (t);

            return tracks;
        }

        /** The mixdown and stems, MIDI and DAWproject files, left in the project's Bounces folder. */
        void exportBounces (ProjectSession& session)
        {
            using namespace ExportService;
            auto& edit = session.getEdit();
            auto bounces = session.getBouncesFolder();
            bounces.createDirectory();

            beginTest ("Mixdown of the whole song");
            {
                Options o;
                o.tailSeconds = 3.0;
                double fileRate = 0;
                int bits = 0;
                auto mix = readBack (exportTo (edit, o, bounces.getChildFile (juce::String (songName) + " mix.wav")), &fileRate, &bits);
                auto readings = measure (mix, fileRate);
                const auto seconds = mix.getNumSamples() / fileRate;
                logMessage ("Mix: " + juce::String (seconds, 2) + " s (song " + juce::String (edit.getLength().inSeconds(), 2) + " s), "
                            + juce::String (bits) + " bit, peak " + juce::String (peakDb (mix), 2) + " dBFS, true peak " + juce::String (readings.truePeakDb, 2)
                            + " dBTP, " + juce::String (readings.integratedLufs, 1) + " LUFS, range " + juce::String (readings.loudnessRangeLu, 1) + " LU");
                expectEquals (bits, 24);
                expectGreaterOrEqual (seconds, edit.getLength().inSeconds() - 0.05, "The whole song");
                expectLessThan (peakDb (mix), -0.5f, "No clipping");
                expectGreaterThan (readings.integratedLufs, -30.0f, "It isn't quiet");
                expectLessThan (rmsDb (mix, seconds - 0.5, seconds, fileRate), -50.0f, "It fades out by the end");
            }

            beginTest ("Stems, as the Export dialog makes them");
            {
                Options o;
                o.tailSeconds = 3.0;
                std::vector<Job::Item> items;
                auto stemFolder = bounces.getChildFile (juce::String (songName) + " stems");
                juce::Array<juce::File> files;
                int i = 1;

                for (auto* t : stemTracks (edit))
                {
                    auto file = stemFolder.getChildFile (juce::String (i++).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t->getName()) + ".wav");
                    items.push_back ({ makeParameters (edit, o, file, { t }), t->getName() });
                    files.add (file);
                }

                expectEquals ((int) items.size(), 10, "Kick, Snare, Hats, Room, Guitar, Bass, Pad, Lead, Reverb and Delay");

                juce::StringArray results;
                expect (runJob (std::move (items), results), results.joinIntoString ("; "));

                for (auto& file : files)
                {
                    auto stem = readBack (file);
                    logMessage (file.getFileNameWithoutExtension() + ": " + juce::String (stem.getNumSamples() / rate, 2) + " s, peak " + juce::String (peakDb (stem), 1) + " dBFS");
                    expectGreaterThan (peakDb (stem), -50.0f, file.getFileName() + " has sound");
                    expectLessThan (peakDb (stem), 0.5f, file.getFileName() + " doesn't clip");
                }

                for (auto* t : te::getAudioTracks (edit))
                    expect (! t->isMuted (false), t->getName() + " isn't left muted by the export");
            }

            beginTest ("MIDI and DAWproject files");
            {
                expect (ExportService::exportMidi (edit, {}, bounces.getChildFile (juce::String (songName) + ".mid")));

                DawProject::Report report;
                auto result = DawProject::exportTo (edit, bounces.getChildFile (juce::String (songName) + DawProject::fileExtension), report);
                expect (result.wasOk(), result.getErrorMessage());
                logMessage ("DAWproject: " + report.summary());
            }
        }

        //==============================================================================
        void checkWhatComesOut (const juce::File& editFile)
        {
            auto& engine = getEngine();

            // On a copy: this also shows a project still works after its folder has moved.
            auto checksFolder = getTestDir().getChildFile ("DemoChecks");
            checksFolder.deleteRecursively();
            checksFolder.createDirectory();
            auto copyFolder = checksFolder.getChildFile (songName);
            expect (editFile.getParentDirectory().copyDirectoryTo (copyFolder));
            auto copyFile = copyFolder.getChildFile (editFile.getFileName());

            Mono reference;
            te::TimeRange wholeSong;
            juce::Array<te::Track*> audioTracks;

            auto recordedTracks = [] (te::Edit& edit)
            {
                juce::Array<te::Track*> tracks;
                for (auto name : { "Kick", "Snare", "Hats", "Room", "Guitar" })
                    if (auto* t = findTrack (edit, name))
                        tracks.add (t);
                return tracks;
            };

            beginTest ("A saved project opens the same after its folder moved");
            {
                {
                    ProjectSession original (engine, editFile);
                    expectWithinAbsoluteError (original.getTransport().getPosition().inSeconds(), 0.0, 0.01, "It opens at the start of the song");
                    wholeSong = { te::TimePosition(), original.getEdit().getLength() };
                    reference = renderClips (original.getEdit(), recordedTracks (original.getEdit()), wholeSong);
                    expect (peakDb (reference) > -40.0f, "The recorded tracks have sound");
                }

                ProjectSession moved (engine, copyFile);
                auto& edit = moved.getEdit();
                expect (MissingMedia::findMissingAudio (edit).isEmpty(), "Audio and takes found in the moved folder");
                expect (MissingMedia::findMissingPlugins (edit).isEmpty(), "All plugins load");
                expectEquals (CompOps::getMaxTakesOnTrack (*findTrack (edit, "Kick")), 3);
                expectEquals (CompOps::getMaxTakesOnTrack (*findTrack (edit, "Bass")), 2);
                expect (GroupEdit::isEnabled (*findFolder (edit, "Drums")), "Group editing stays on");
                expectEquals (KeyScale::getKey (edit).getName(), juce::String ("D Minor"));
                expect (SongNotes::getProjectNotes (edit).isNotEmpty());
                expectEquals (edit.tempoSequence.getNumTempos(), 2);

                auto difference = differenceDb (reference, renderClips (edit, recordedTracks (edit), wholeSong));
                logMessage ("Reopened against saved: difference " + juce::String (difference, 1) + " dB");
                expectLessThan (difference, -90.0f, "It plays the same");
            }

            ProjectSession session (engine, copyFile);
            auto& edit = session.getEdit();
            auto& um = edit.getUndoManager();
            auto current = [&] { return renderClips (edit, recordedTracks (edit), wholeSong); };

            auto expectUnchanged = [&] (const juce::String& what)
            {
                auto difference = differenceDb (reference, current());
                logMessage (what + ": difference " + juce::String (difference, 1) + " dB");
                expectLessThan (difference, -90.0f, what + " sounds the same");
            };

            beginTest ("Editing takes doesn't change the sound by accident");
            {
                auto kickClips = findTrack (edit, "Kick")->getClips().size();

                um.beginNewTransaction ("Delete unused");
                for (auto* name : { "Kick", "Guitar" })
                    CompOps::deleteUnusedTakes (*findTrack (edit, name)->getClips()[0]);
                expectUnchanged ("Delete Unused Takes");
                um.undo();
                expectUnchanged ("Undoing that");

                um.beginNewTransaction ("Flatten");
                for (auto* name : { "Kick", "Snare", "Hats", "Room", "Guitar" })
                {
                    juce::ReferenceCountedArray<te::Clip> clips;
                    for (auto c : findTrack (edit, name)->getClips())
                        clips.add (c);
                    for (auto c : clips)
                        CompOps::removeTakes (*c);
                }

                expectEquals (CompOps::getMaxTakesOnTrack (*findTrack (edit, "Kick")), 0);
                expectEquals (findTrack (edit, "Kick")->getClips().size(), kickClips);
                expectUnchanged ("Flatten");
                um.undo();
                expectEquals (CompOps::getMaxTakesOnTrack (*findTrack (edit, "Kick")), 3);
                expectUnchanged ("Undoing that");

                // Switching a take and back: pieces rejoin, the sound returns.
                auto* kick = findTrack (edit, "Kick");
                um.beginNewTransaction ("Switch");
                CompOps::useTake (*clipAt (*kick, barToSeconds (15)), 0);
                expect (differenceDb (reference, current()) > -30.0f, "Another take does change the sound");
                um.undo();
                expectUnchanged ("Undoing a take switch");

                // A section moves with its takes.
                auto& ts = edit.tempoSequence;
                auto barTime = [&] (double bar) { return ts.toTime (te::BeatPosition::fromBeats (bar * 4.0)); };
                te::ArrangerClip* secondChorus = nullptr;

                for (auto* s : ArrangerOps::getSections (edit))
                    if (s->getName() == "Chorus" && s->getPosition().getStart() > barTime (20))
                        secondChorus = s;

                expect (secondChorus != nullptr);

                if (secondChorus != nullptr)
                {
                    te::TimeRange before (barTime (24), barTime (32));
                    auto wanted = renderClips (edit, recordedTracks (edit), before);
                    const auto layoutBefore = describeLayout (edit);

                    um.beginNewTransaction ("Move");
                    ArrangerOps::moveSection (edit, *secondChorus, barTime (4));
                    auto moved = renderClips (edit, recordedTracks (edit), { barTime (4), barTime (12) });
                    double where = 0.0;
                    auto difference = differenceDb (wanted, moved, &where);
                    logMessage ("Chorus moved through the arranger: difference " + juce::String (difference, 1) + " dB, " + juce::String (where, 3) + " s in");
                    expectLessThan (difference, -60.0f, "The moved section plays what it did");
                    expect (CompOps::hasTakes (*clipAt (*findTrack (edit, "Kick"), barToSeconds (5))), "It keeps its takes");
                    um.undo();

                    if (auto layoutAfter = describeLayout (edit); layoutAfter != layoutBefore)
                    {
                        logMessage ("After undoing the move, the layout differs. Before:\n" + layoutBefore);
                        logMessage ("After:\n" + layoutAfter);
                    }

                    expectUnchanged ("Undoing the move");
                }
            }

            checkExports (session, checksFolder);
            checkMidiFile (session, checksFolder);
            checkDawProject (session, checksFolder, recordedTracks);
            checkStemsAddUp (session, checksFolder);
            checkProjectWorkflow (session, checksFolder);
        }

        void checkExports (ProjectSession& session, const juce::File& folder)
        {
            using namespace ExportService;
            auto& edit = session.getEdit();
            auto& ts = edit.tempoSequence;
            te::TimeRange chorus (ts.toTime (te::BeatPosition::fromBeats (12 * 4.0)), ts.toTime (te::BeatPosition::fromBeats (20 * 4.0)));

            Options base;
            base.range = Range::custom;
            base.customRange = chorus;
            base.tailSeconds = 0.0;
            base.dither = false;

            auto reference = readBack (exportTo (edit, base, folder.getChildFile ("chorus.wav")));
            auto referenceReadings = measure (reference, rate);
            logMessage ("Chorus: peak " + juce::String (peakDb (reference), 2) + " dBFS, " + juce::String (referenceReadings.integratedLufs, 2) + " LUFS");

            beginTest ("The song in each format");
            {
                expectWithinAbsoluteError (reference.getNumSamples() / rate, chorus.getLength().inSeconds(), 0.02, "A range exports just the range");

                for (auto format : { Format::flac, Format::ogg, Format::mp3 })
                {
                    auto name = getExtension (format).substring (1);

                    if (! isFormatAvailable (edit.engine, format))
                    {
                        logMessage (name + ": not available here (MP3 needs libmp3lame.dll next to CDAW.exe)");
                        continue;
                    }

                    auto o = base;
                    o.format = format;
                    double fileRate = 0;
                    auto file = exportTo (edit, o, folder.getChildFile ("chorus_" + name + getExtension (format)));
                    auto b = readBack (file, &fileRate);
                    auto readings = measure (b, fileRate);
                    logMessage (name + ": " + juce::String (file.getSize() / 1024) + " KB, " + juce::String (b.getNumSamples() / fileRate, 2) + " s, "
                                + juce::String (readings.integratedLufs, 2) + " LUFS");
                    expectWithinAbsoluteError (b.getNumSamples() / fileRate, chorus.getLength().inSeconds(), 0.1, name + " length");
                    expectWithinAbsoluteError (readings.integratedLufs, referenceReadings.integratedLufs, 1.0f, name + " loudness");
                }

                for (auto [depth, sampleRate] : { std::pair { 16, 44100.0 }, std::pair { 32, 96000.0 } })
                {
                    auto o = base;
                    o.bitDepth = depth;
                    o.sampleRate = sampleRate;
                    o.dither = depth < 32;
                    double fileRate = 0;
                    int bits = 0;
                    auto b = readBack (exportTo (edit, o, folder.getChildFile ("chorus_" + juce::String (depth) + "_" + juce::String ((int) sampleRate) + ".wav")), &fileRate, &bits);
                    auto readings = measure (b, fileRate);
                    logMessage (juce::String (depth) + " bit, " + juce::String (fileRate / 1000.0, 1) + " kHz: " + juce::String (b.getNumSamples() / fileRate, 2) + " s, "
                                + juce::String (readings.integratedLufs, 2) + " LUFS");
                    expectEquals (bits, depth);
                    expectWithinAbsoluteError (fileRate, sampleRate, 0.1);
                    expectWithinAbsoluteError (b.getNumSamples() / fileRate, chorus.getLength().inSeconds(), 0.02);
                    expectWithinAbsoluteError (readings.integratedLufs, referenceReadings.integratedLufs, 1.0f, "Same loudness at another format");
                }
            }

            beginTest ("Normalising the song");
            {
                auto o = base;
                o.normalise = Normalise::peak;
                o.normaliseLevel = -3.0f;
                auto peaked = readBack (exportTo (edit, o, folder.getChildFile ("chorus_peak.wav")));
                logMessage ("Normalised to -3 dBFS: " + juce::String (peakDb (peaked), 2) + " dBFS");
                expectWithinAbsoluteError (peakDb (peaked), -3.0f, 0.2f);

                o.normalise = Normalise::lufs;
                o.normaliseLevel = -16.0f;
                o.truePeakCeiling = -1.0f;
                auto loud = readBack (exportTo (edit, o, folder.getChildFile ("chorus_lufs.wav")));
                auto readings = measure (loud, rate);
                logMessage ("Normalised to -16 LUFS: " + juce::String (readings.integratedLufs, 2) + " LUFS, " + juce::String (readings.truePeakDb, 2) + " dBTP");
                expectWithinAbsoluteError (readings.integratedLufs, -16.0f, 0.5f);
                expectLessThan (readings.truePeakDb, -0.7f);

                o.normaliseLevel = -6.0f;
                auto pushed = readBack (exportTo (edit, o, folder.getChildFile ("chorus_lufs_pushed.wav")));
                auto pushedReadings = measure (pushed, rate);
                logMessage ("Asked for -6 LUFS: " + juce::String (pushedReadings.integratedLufs, 2) + " LUFS, " + juce::String (pushedReadings.truePeakDb, 2) + " dBTP");
                expectLessThan (pushedReadings.truePeakDb, -0.7f, "The ceiling wins over the loudness asked for");
            }

            beginTest ("The tail, the loop range and what's muted");
            {
                auto o = base;
                o.tailSeconds = 4.0;
                auto withTail = readBack (exportTo (edit, o, folder.getChildFile ("chorus_tail.wav")));
                const auto end = chorus.getLength().inSeconds();
                logMessage ("With a 4 s tail: " + juce::String (withTail.getNumSamples() / rate, 2) + " s");
                expectGreaterThan (withTail.getNumSamples() / rate, end + 1.0, "The file runs past the range");
                expectGreaterThan (rmsDb (withTail, end + 0.2, end + 1.0, rate), -70.0f, "Reverb and delay are still sounding");

                // The song plays on under a range: the tail hears the bridge, not silence? Report what it does.
                edit.getTransport().setLoopRange (chorus);
                auto looped = base;
                looped.range = Range::loop;
                auto loop = readBack (exportTo (edit, looped, folder.getChildFile ("chorus_loop.wav")));
                expectWithinAbsoluteError (loop.getNumSamples() / rate, chorus.getLength().inSeconds(), 0.02);
                edit.getTransport().setLoopRange ({});

                // Muted clips stay out: the bridge has no drums.
                auto& ts2 = edit.tempoSequence;
                // (From a little after the bar line: the recording before it stopped just past it.)
                te::TimeRange bridge (ts2.toTime (te::BeatPosition::fromBeats (20.25 * 4.0)), ts2.toTime (te::BeatPosition::fromBeats (24 * 4.0)));
                auto* kick = findTrack (edit, "Kick");
                auto stem = base;
                stem.customRange = bridge;
                auto kickBridge = readBack (exportTo (edit, stem, folder.getChildFile ("kick_bridge.wav"), { kick }));
                logMessage ("Kick stem through the muted bridge: peak " + juce::String (peakDb (kickBridge), 1) + " dBFS");
                expectLessThan (peakDb (kickBridge), -100.0f, "A muted clip isn't in the stem");
            }
        }

        void checkMidiFile (ProjectSession& session, const juce::File& folder)
        {
            beginTest ("The MIDI file: what plays, where it plays");

            auto& edit = session.getEdit();
            auto dest = folder.getChildFile ("song.mid");
            expect (ExportService::exportMidi (edit, {}, dest));

            juce::MidiFile file;
            juce::FileInputStream in (dest);
            expect (file.readFrom (in));
            expectEquals (file.getNumTracks(), 4, "Tempo, Bass, Pad and Lead");

            int tempoEvents = 0;
            if (auto* tempoTrack = file.getTrack (0))
                for (auto* e : *tempoTrack)
                    if (e->message.isTempoMetaEvent())
                        ++tempoEvents;

            expectEquals (tempoEvents, 2, "The tempo change is in the file");

            const juce::MidiMessageSequence* bass = nullptr;

            for (int t = 1; t < file.getNumTracks(); ++t)
                for (auto* e : *file.getTrack (t))
                    if (e->message.isTrackNameEvent() && e->message.getTextFromTextMetaEvent() == "Bass")
                        bass = file.getTrack (t);

            expect (bass != nullptr, "A track named Bass");

            if (bass == nullptr)
                return;

            auto ticksPerBeat = (double) file.getTimeFormat();
            auto countAt = [&] (double fromBar, double toBar, std::function<bool (int)> isIt)
            {
                int n = 0;
                for (auto* e : *bass)
                {
                    const auto beat = e->message.getTimeStamp() / ticksPerBeat;
                    if (e->message.isNoteOn() && beat >= fromBar * 4.0 - 0.1 && beat < toBar * 4.0 - 0.1 && isIt (e->message.getNoteNumber()))
                        ++n;
                }
                return n;
            };

            auto all = [] (int) { return true; };
            const auto takeTwoInBars = countAt (12, 16, all);
            const auto takeOneInBars = countAt (16, 20, all);
            logMessage ("Bass in the MIDI file: " + juce::String (takeTwoInBars) + " notes in bars 12-16 (take 2 has 32), "
                        + juce::String (takeOneInBars) + " in bars 16-20 (take 1 has 24)");
            expectEquals (takeTwoInBars, 32, "The take that plays there, not the other one as well");
            expectEquals (takeOneInBars, 24);
            expectEquals (countAt (24, 28, all), 32, "The copied chorus starts on take 2 as well");
        }

        template <typename TrackList>
        void checkDawProject (ProjectSession& session, const juce::File& folder, TrackList recordedTracks)
        {
            beginTest ("DAWproject there and back");

            auto& edit = session.getEdit();
            auto file = folder.getChildFile (juce::String ("song") + DawProject::fileExtension);
            DawProject::Report out;
            auto exported = DawProject::exportTo (edit, file, out);
            expect (exported.wasOk(), exported.getErrorMessage());

            auto backFile = ProjectSession::createProjectFolder (folder, "Back from DAWproject");
            ProjectSession back (getEngine(), backFile);
            DawProject::Report in;
            auto imported = DawProject::importInto (back.getEdit(), file, back.getAudioFolder().getChildFile ("Imported"), in);
            expect (imported.wasOk(), imported.getErrorMessage());
            logMessage ("Back in: " + in.summary());

            auto& edit2 = back.getEdit();

            // Clip by clip, to see if anything's lost.
            for (auto* t : te::getAudioTracks (edit))
            {
                auto* other = findTrack (edit2, t->getName());
                logMessage (t->getName() + ": " + juce::String (t->getClips().size()) + " clips, " + juce::String (other != nullptr ? other->getClips().size() : -1) + " after");
            }

            expectEquals (edit2.tempoSequence.getNumTempos(), 2, "The tempo change comes back");

            for (auto name : { "Kick", "Guitar", "Bass", "Pad", "Lead", "Reverb", "Delay" })
                expect (findTrack (edit2, name) != nullptr, juce::String (name) + " comes back");

            te::TimeRange range { te::TimePosition(), edit.getLength() };
            auto before = renderClips (edit, recordedTracks (edit), range);
            auto after = renderClips (edit2, recordedTracks (edit2), range);
            auto difference = differenceDb (before, after);
            logMessage ("The recorded tracks there and back: difference " + juce::String (difference, 1) + " dB");
            expectLessThan (difference, -50.0f, "The comp plays the same after going through the format");

            int notesBefore = 0, notesAfter = 0;
            for (auto c : findTrack (edit, "Bass")->getClips())
                if (auto mc = dynamic_cast<te::MidiClip*> (c))
                    notesBefore += mc->getSequence().getNumNotes();

            if (auto* bass = findTrack (edit2, "Bass"))
                for (auto c : bass->getClips())
                    if (auto mc = dynamic_cast<te::MidiClip*> (c))
                        notesAfter += mc->getSequence().getNumNotes();

            logMessage ("Bass notes: " + juce::String (notesBefore) + " across all takes before, " + juce::String (notesAfter) + " after");
            expectGreaterThan (notesAfter, 0);
        }

        /** With nothing shared between the stems (no group or master processing, no FX), they must add up to the mix. */
        void checkStemsAddUp (ProjectSession& session, const juce::File& folder)
        {
            using namespace ExportService;
            beginTest ("The stems add up to the mix");

            auto& edit = session.getEdit();
            auto& um = edit.getUndoManager();
            um.beginNewTransaction ("Just what adds up");

            // Synths with free-running phases and shared processing come out; what's left is deterministic.
            juce::Array<te::Track*> remove;
            for (auto name : { "Bass", "Pad", "Lead", "Reverb", "Delay" })
                if (auto* t = findTrack (edit, name))
                    remove.add (t);

            TrackOps::deleteTracks (edit, remove);

            for (auto p : edit.getMasterPluginList().getPlugins())
                p->setEnabled (false);

            auto* master = edit.getMasterVolumePlugin().get();
            master->volParam->getCurve().clear (nullptr);
            master->setVolumeDb (0.0f);

            if (auto* group = findFolder (edit, "Drums"))
            {
                for (auto p : group->pluginList.getPlugins())
                    if (dynamic_cast<CompressorPlugin*> (p) != nullptr)
                        p->setEnabled (false);
            }

            Options o;
            o.bitDepth = 32;
            o.tailSeconds = 0.0;
            o.dither = false;

            auto mix = readBack (exportTo (edit, o, folder.getChildFile ("addup_mix.wav")));
            juce::AudioBuffer<float> sum (mix.getNumChannels(), mix.getNumSamples());
            sum.clear();

            for (auto* t : stemTracks (edit))
            {
                auto stem = readBack (exportTo (edit, o, folder.getChildFile ("addup_" + t->getName() + ".wav"), { t }));
                for (int ch = 0; ch < sum.getNumChannels(); ++ch)
                    sum.addFrom (ch, 0, stem, juce::jmin (ch, stem.getNumChannels() - 1), 0, juce::jmin (sum.getNumSamples(), stem.getNumSamples()));
            }

            auto difference = differenceDb (mix, sum);
            logMessage ("Stems summed against the mix: difference " + juce::String (difference, 1) + " dB (mix peak " + juce::String (peakDb (mix), 1) + " dBFS)");
            expectLessThan (difference, -60.0f, "Nothing is lost or doubled in the stems");

            um.undo();
            for (auto p : edit.getMasterPluginList().getPlugins())
                p->setEnabled (true);
        }

        void checkProjectWorkflow (ProjectSession& session, const juce::File& folder)
        {
            auto& engine = getEngine();
            auto& edit = session.getEdit();
            auto drumsState = [] (te::Edit& e) { return findFolder (e, "Drums"); };

            beginTest ("Save New Version");
            {
                edit.markAsChanged();
                auto firstFile = session.getEditFile();
                auto second = session.saveNewVersion ("With the slower outro");
                expect (second.existsAsFile());
                expectEquals (second.getFileNameWithoutExtension(), juce::String (songName) + "_02");
                expect (firstFile.existsAsFile(), "The version before stays");
                auto versions = ProjectVersions::list (second);
                expectEquals (versions.size(), 2);
                expectEquals (ProjectVersions::readNote (second), juce::String ("With the slower outro"));

                ProjectSession opened (engine, second);
                expect (MissingMedia::findMissingAudio (opened.getEdit()).isEmpty(), "Shares the first version's audio and takes");
            }

            beginTest ("Save As to a new folder");
            {
                auto target = folder.getChildFile ("Saved As").getChildFile ("Harbour Copy.cdaw");
                expect (session.saveAs (target));
                {
                    ProjectSession opened (engine, target);
                    expect (MissingMedia::findMissingAudio (opened.getEdit()).isEmpty(), "Audio and takes still found from the new folder");
                    expectEquals (CompOps::getMaxTakesOnTrack (*findTrack (opened.getEdit(), "Kick")), 3);
                }
            }

            beginTest ("Template and a new song from it");
            {
                auto templateFile = folder.getChildFile (juce::String ("Demo") + TemplateOps::templateSuffix);
                expect (TemplateOps::saveTemplate (edit, templateFile));

                auto newFile = ProjectSession::createProjectFolder (folder, "From Template");
                expect (TemplateOps::createProjectFromTemplate (templateFile, newFile));

                ProjectSession fresh (engine, newFile);
                auto& e = fresh.getEdit();
                int clips = 0;
                for (auto* t : te::getAudioTracks (e))
                    clips += t->getClips().size();

                expectEquals (clips, 0, "No clips");
                expectEquals (te::getAudioTracks (e).size(), te::getAudioTracks (edit).size(), "The tracks");
                expect (drumsState (e) != nullptr && GroupEdit::isEnabled (*drumsState (e)), "The drum group, with group editing on");

                if (auto* snare = findTrack (e, "Snare"))
                    expectEquals (MixerOps::getSends (*snare).size(), 1, "Its send");

                expectEquals (KeyScale::getKey (e).getName(), juce::String ("D Minor"), "The key");
                expect (SongNotes::getProjectNotes (e).isEmpty(), "Not the notes");
                expect (ArrangerOps::getSections (e).isEmpty(), "Not the sections");
            }

            beginTest ("The drum group as a track preset");
            {
                auto presetFile = folder.getChildFile (juce::String ("Drums") + TemplateOps::trackPresetSuffix);
                auto* drums = drumsState (edit);
                expect (drums != nullptr && TemplateOps::saveTrackPreset (*drums, presetFile));

                auto newFile = ProjectSession::createProjectFolder (folder, "Preset Song");
                ProjectSession other (engine, newFile);
                auto& e = other.getEdit();

                auto fxTrack = TrackOps::addFxTrack (e, nullptr);
                fxTrack->setName ("Reverb");
                e.setAuxBusName (MixerOps::getFxBuses (e).getFirst().number, "Reverb");

                auto inserted = TemplateOps::insertTrackPreset (e, presetFile, nullptr, &other.getSelectionManager());
                expect (inserted != nullptr);
                auto* group = dynamic_cast<te::FolderTrack*> (inserted.get());
                expect (group != nullptr);

                if (group != nullptr)
                {
                    expectEquals (group->getAllSubTracks (true).size(), 4, "Its four tracks");
                    expect (GroupEdit::isEnabled (*group), "Group editing comes along");

                    for (auto* t : group->getAllSubTracks (true))
                        if (auto* at = dynamic_cast<te::AudioTrack*> (t))
                            expect (at->getClips().isEmpty(), "No clips");
                }
            }
        }
    };

    static DemoProject demoProject;
}
