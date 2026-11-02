#include "TestEngine.h"
#include "Model/MixerOps.h"
#include "Model/TrackOps.h"
#include "Plugins/CompressorPlugin.h"
#include "Plugins/ReverbPlugin.h"
#include "Render/ExportService.h"

namespace cdaw::test
{
    struct ExportTests : public juce::UnitTest
    {
        ExportTests() : juce::UnitTest ("Export", "CDAW") {}

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

        void runTest() override
        {
            auto& engine = getEngine();
            auto outDir = getTestDir().getChildFile ("exports");
            outDir.deleteRecursively();
            outDir.createDirectory();

            auto edit = te::test_utilities::createTestEdit (engine, 2, te::Edit::forRendering);
            auto t1 = te::getAudioTracks (*edit)[0];
            auto t2 = te::getAudioTracks (*edit)[1];
            t1->insertWaveClip ("a", writeSineFile ("exp_a", 3.0, 0.25f), { { te::TimePosition(), te::TimeDuration::fromSeconds (3.0) }, {} }, false);
            t2->insertWaveClip ("b", writeSineFile ("exp_b", 2.0, 0.25f, 500.0), { { te::TimePosition::fromSeconds (1.0), te::TimeDuration::fromSeconds (2.0) }, {} }, false);

            beginTest ("Mixdown: 24-bit / 48 kHz WAV with the song length");
            {
                ExportService::Options o;
                o.bitDepth = 24;
                o.sampleRate = 48000.0;
                o.tailSeconds = 0.0;
                o.dither = false;
                auto dest = outDir.getChildFile ("mix.wav");

                juce::StringArray results;
                expect (runJob ({ { ExportService::makeParameters (*edit, o, dest, {}), "Mixdown" } }, results), results.joinIntoString ("; "));
                expect (dest.existsAsFile());

                juce::AudioFormatManager fm;
                fm.registerBasicFormats();

                if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (dest)))
                {
                    expectEquals ((int) reader->bitsPerSample, 24);
                    expectWithinAbsoluteError (reader->sampleRate, 48000.0, 0.1);
                    expectWithinAbsoluteError ((double) reader->lengthInSamples / reader->sampleRate, 3.0, 0.05);
                }
                else
                {
                    expect (false, "Couldn't read the exported file");
                }
            }

            beginTest ("A range of the song (a selection or typed times), and the loop range");
            {
                ExportService::Options o;
                o.tailSeconds = 0.0;
                o.dither = false;
                o.range = ExportService::Range::custom;
                o.customRange = { te::TimePosition::fromSeconds (0.5), te::TimePosition::fromSeconds (1.75) };
                auto dest = outDir.getChildFile ("range.wav");

                juce::StringArray results;
                expect (runJob ({ { ExportService::makeParameters (*edit, o, dest, {}), "Range" } }, results), results.joinIntoString ("; "));
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (dest)))
                    expectWithinAbsoluteError ((double) reader->lengthInSamples / reader->sampleRate, 1.25, 0.01, "just the range");
                else
                    expect (false, "Couldn't read the exported file");

                o.customRange = {};
                expect (ExportService::getRenderRange (*edit, o).isEmpty(), "no range chosen: nothing to export");

                edit->getTransport().setLoopRange ({ te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (2.0) });
                o.range = ExportService::Range::loop;
                expectWithinAbsoluteError (ExportService::getRenderRange (*edit, o).getLength().inSeconds(), 1.0, 1.0e-9);
            }

            beginTest ("Stems: one file per track");
            {
                ExportService::Options o;
                o.tailSeconds = 0.0;
                std::vector<ExportService::Job::Item> items;
                items.push_back ({ ExportService::makeParameters (*edit, o, outDir.getChildFile ("stem1.wav"), { t1 }), "1" });
                items.push_back ({ ExportService::makeParameters (*edit, o, outDir.getChildFile ("stem2.wav"), { t2 }), "2" });

                juce::StringArray results;
                expect (runJob (std::move (items), results), results.joinIntoString ("; "));
                expect (outDir.getChildFile ("stem1.wav").existsAsFile());
                expect (outDir.getChildFile ("stem2.wav").existsAsFile());
            }

            beginTest ("A muted track is left out of the mixdown");
            {
                auto rmsOfExport = [&] (const juce::File& dest)
                {
                    juce::AudioFormatManager fm;
                    fm.registerBasicFormats();
                    auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (dest));

                    if (reader == nullptr)
                        return -1.0f;

                    juce::AudioBuffer<float> buf (1, (int) reader->sampleRate);   // 1.5 .. 2.5 s: both tracks play
                    reader->read (&buf, 0, buf.getNumSamples(), (juce::int64) (reader->sampleRate * 1.5), true, false);
                    return buf.getRMSLevel (0, 0, buf.getNumSamples());
                };

                ExportService::Options o;
                o.tailSeconds = 0.0;
                o.dither = false;
                juce::StringArray results;

                auto both = outDir.getChildFile ("mute_both.wav");
                expect (runJob ({ { ExportService::makeParameters (*edit, o, both, {}), "Both" } }, results));

                t2->setMute (true);
                auto oneMuted = outDir.getChildFile ("mute_one.wav");
                expect (runJob ({ { ExportService::makeParameters (*edit, o, oneMuted, {}), "Muted" } }, results));
                t2->setMute (false);

                auto rmsBoth = rmsOfExport (both), rmsMuted = rmsOfExport (oneMuted);
                logMessage ("Mixdown RMS with both tracks " + juce::String (rmsBoth, 4) + ", with track 2 muted " + juce::String (rmsMuted, 4));
                expectWithinAbsoluteError (rmsMuted, 0.25f / std::sqrt (2.0f), 0.02f);
                expectGreaterThan (rmsBoth, rmsMuted * 1.2f);
            }

            beginTest ("MIDI export round trip");
            {
                auto midiEdit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*midiEdit)[0];
                auto mc = track->insertMIDIClip ({ te::TimePosition(), te::TimePosition::fromSeconds (10.0) }, nullptr);   // 60 bpm: 10 beats

                for (int i = 0; i < 8; ++i)
                    mc->getSequence().addNote (36 + i, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

                auto dest = outDir.getChildFile ("song.mid");
                expect (ExportService::exportMidi (*midiEdit, {}, dest));

                juce::MidiFile mf;
                juce::FileInputStream in (dest);
                expect (mf.readFrom (in));
                expectEquals (mf.getNumTracks(), 2);

                int noteOns = 0;
                if (auto* seq = mf.getTrack (1))
                    for (auto* ev : *seq)
                        if (ev->message.isNoteOn())
                            ++noteOns;

                expectEquals (noteOns, 8);
            }

            beginTest ("MIDI export: clips are where they play, showing what they play");
            {
                auto midiEdit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*midiEdit)[0];   // 60 bpm: a beat is a second

                // Notes on beats 0-7; the clip starts on beat 10, showing beats 2-6 of them.
                auto mc = track->insertMIDIClip ({ te::TimePosition::fromSeconds (10.0), te::TimePosition::fromSeconds (14.0) }, nullptr);
                mc->setOffset (te::TimeDuration::fromSeconds (2.0));

                for (int i = 0; i < 8; ++i)
                    mc->getSequence().addNote (60 + i, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

                // A note overlapping the clip's end is cut there.
                mc->getSequence().addNote (80, te::BeatPosition::fromBeats (5.5), te::BeatDuration::fromBeats (3.0), 100, 0, nullptr);

                auto muted = track->insertMIDIClip ({ te::TimePosition::fromSeconds (20.0), te::TimePosition::fromSeconds (22.0) }, nullptr);
                muted->getSequence().addNote (90, {}, te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
                muted->setMuted (true);

                auto dest = outDir.getChildFile ("placed.mid");
                expect (ExportService::exportMidi (*midiEdit, {}, dest));

                juce::MidiFile mf;
                juce::FileInputStream in (dest);
                expect (mf.readFrom (in));
                mf.convertTimestampTicksToSeconds();

                juce::Array<int> notes;
                double firstOn = -1.0, overlapOff = -1.0;

                if (auto* seq = mf.getTrack (1))
                {
                    for (auto* ev : *seq)
                    {
                        if (ev->message.isNoteOn())
                        {
                            notes.add (ev->message.getNoteNumber());
                            if (firstOn < 0.0)
                                firstOn = ev->message.getTimeStamp();
                        }
                        else if (ev->message.isNoteOff() && ev->message.getNoteNumber() == 80)
                        {
                            overlapOff = ev->message.getTimeStamp();
                        }
                    }
                }

                expect (notes == juce::Array<int> { 62, 63, 64, 65, 80 }, "What the clip shows, not all the notes it holds, nor the muted clip: " + juce::String (notes.size()) + " notes");
                expectWithinAbsoluteError (firstOn, 10.0, 0.01, "The clip's first note where the clip plays it (the file is at 60 bpm)");
                expectWithinAbsoluteError (overlapOff, 14.0, 0.01, "A note over the clip's end stops with the clip");
            }

            exportFormats (*edit, *t1, *t2, outDir);
            stemsThroughRouting (outDir);
        }

        /** The file as it reads back, through the engine's own readers (MP3 and OGG included). */
        juce::AudioBuffer<float> readBack (const juce::File& file, double* sampleRate = nullptr, int* bits = nullptr)
        {
            auto& formats = getEngine().getAudioFileFormatManager().readFormatManager;
            juce::AudioBuffer<float> buffer;

            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)))
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

        static float integratedLufs (const juce::AudioBuffer<float>& b, double sampleRate)
        {
            te::LoudnessMeter meter;
            meter.prepare (sampleRate, b.getNumChannels(), 4096);
            meter.process (b.getArrayOfReadPointers(), b.getNumChannels(), b.getNumSamples());
            meter.flush();
            return meter.getReadings().integratedLufs;
        }

        static float truePeakDb (const juce::AudioBuffer<float>& b, double sampleRate)
        {
            te::LoudnessMeter meter;
            meter.prepare (sampleRate, b.getNumChannels(), 4096);
            meter.process (b.getArrayOfReadPointers(), b.getNumChannels(), b.getNumSamples());
            meter.flush();
            return meter.getReadings().truePeakDb;
        }

        static float peakDb (const juce::AudioBuffer<float>& b)
        {
            return juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -150.0f);
        }

        void exportFormats (te::Edit& edit, te::AudioTrack&, te::AudioTrack&, const juce::File& outDir)
        {
            using namespace ExportService;

            auto render = [&] (Options o, const juce::String& name)
            {
                auto dest = outDir.getChildFile (name + getExtension (o.format));
                juce::StringArray results;
                expect (runJob ({ { makeParameters (edit, o, dest, {}), name } }, results), name + ": " + results.joinIntoString ("; "));
                expect (dest.existsAsFile(), name + " written");
                return dest;
            };

            Options base;
            base.tailSeconds = 0.0;
            base.dither = false;

            auto reference = readBack (render (base, "reference"));
            const auto referencePeak = peakDb (reference);

            beginTest ("Export: bit depths and sample rates");
            {
                for (int depth : { 16, 24, 32 })
                {
                    auto o = base;
                    o.bitDepth = depth;
                    double rate = 0;
                    int bits = 0;
                    auto b = readBack (render (o, "depth" + juce::String (depth)), &rate, &bits);
                    expectEquals (bits, depth);
                    expectWithinAbsoluteError (peakDb (b), referencePeak, 0.05f, "same level at " + juce::String (depth) + " bit");
                }

                auto o = base;
                o.sampleRate = 44100.0;
                double rate = 0;
                auto b = readBack (render (o, "rate44"), &rate);
                expectWithinAbsoluteError (rate, 44100.0, 0.1);
                expectWithinAbsoluteError ((double) b.getNumSamples() / rate, 3.0, 0.02, "same length at 44.1 kHz");
                expectWithinAbsoluteError (peakDb (b), referencePeak, 0.3f);   // (the peak falls between other samples)
            }

            beginTest ("Export: FLAC, OGG and MP3");
            {
                for (auto format : { Format::flac, Format::ogg, Format::mp3 })
                {
                    auto name = getExtension (format).substring (1);

                    if (! isFormatAvailable (edit.engine, format))
                    {
                        logMessage ("Export format not available here: " + name);
                        continue;
                    }

                    auto o = base;
                    o.format = format;
                    double rate = 0;
                    auto dest = render (o, "format_" + name);
                    auto b = readBack (dest, &rate);
                    logMessage (name + ": " + juce::String (dest.getSize() / 1024) + " KB, " + juce::String ((double) b.getNumSamples() / juce::jmax (1.0, rate), 3)
                                + " s, peak " + juce::String (peakDb (b), 2) + " dBFS (WAV " + juce::String (referencePeak, 2) + ")");
                    expectWithinAbsoluteError ((double) b.getNumSamples() / juce::jmax (1.0, rate), 3.0, 0.1, name + " length");
                    expectWithinAbsoluteError (peakDb (b), referencePeak, 1.0f, name + " level");
                }
            }

            beginTest ("Export: dither changes the 16-bit file, not its level");
            {
                auto o = base;
                o.bitDepth = 16;
                auto plain = readBack (render (o, "dither_off"));
                o.dither = true;
                auto dithered = readBack (render (o, "dither_on"));

                int different = 0;
                for (int i = 0; i < juce::jmin (plain.getNumSamples(), dithered.getNumSamples()); ++i)
                    if (plain.getSample (0, i) != dithered.getSample (0, i))
                        ++different;

                expectGreaterThan (different, plain.getNumSamples() / 4, "dither touches the samples");
                expectWithinAbsoluteError (peakDb (dithered), peakDb (plain), 0.05f);
            }

            beginTest ("Export: normalising to a peak and to a loudness");
            {
                auto o = base;
                o.normalise = Normalise::peak;
                o.normaliseLevel = -1.0f;
                auto peak = readBack (render (o, "norm_peak"));
                logMessage ("Peak normalised: " + juce::String (peakDb (peak), 2) + " dBFS (was " + juce::String (referencePeak, 2) + ")");
                expectWithinAbsoluteError (peakDb (peak), -1.0f, 0.1f);

                o.normalise = Normalise::lufs;
                o.normaliseLevel = -14.0f;
                o.truePeakCeiling = -1.0f;
                auto lufs = readBack (render (o, "norm_lufs"));
                logMessage ("Loudness normalised: " + juce::String (integratedLufs (lufs, 48000.0), 2) + " LUFS, true peak "
                            + juce::String (truePeakDb (lufs, 48000.0), 2) + " dBTP");
                expectWithinAbsoluteError (integratedLufs (lufs, 48000.0), -14.0f, 0.5f);
                expectLessThan (truePeakDb (lufs, 48000.0), -0.8f, "the ceiling holds");

                // Asked for more than the ceiling allows: the ceiling wins.
                o.normaliseLevel = -3.0f;
                auto loud = readBack (render (o, "norm_lufs_ceiling"));
                logMessage ("Loudness -3 LUFS asked for: " + juce::String (integratedLufs (loud, 48000.0), 2) + " LUFS, true peak "
                            + juce::String (truePeakDb (loud, 48000.0), 2) + " dBTP");
                expectLessThan (truePeakDb (loud, 48000.0), -0.8f, "the ceiling holds when pushed");
            }

            beginTest ("Export: 32-bit float keeps what's over 0 dBFS");
            {
                auto hot = te::test_utilities::createTestEdit (getEngine(), 1, te::Edit::forRendering);
                auto t = te::getAudioTracks (*hot)[0];
                t->insertWaveClip ("a", writeSineFile ("exp_hot", 2.0, 0.9f), { { te::TimePosition(), te::TimeDuration::fromSeconds (2.0) }, {} }, false);
                t->getVolumePlugin()->setVolumeDb (6.0f);

                Options o;
                o.bitDepth = 32;
                o.tailSeconds = 0.0;
                auto dest = outDir.getChildFile ("hot.wav");
                juce::StringArray results;
                expect (runJob ({ { makeParameters (*hot, o, dest, {}), "Hot" } }, results));
                auto b = readBack (dest);
                logMessage ("Float export of a track 5 dB over: peak " + juce::String (peakDb (b), 2) + " dBFS");
                expectGreaterThan (peakDb (b), 3.0f);
            }

            beginTest ("Export: the tail lets a reverb ring out");
            {
                auto wet = te::test_utilities::createTestEdit (getEngine(), 1, te::Edit::forRendering);
                auto t = te::getAudioTracks (*wet)[0];
                t->insertWaveClip ("a", writeSineFile ("exp_wet", 1.0, 0.5f), { { te::TimePosition(), te::TimeDuration::fromSeconds (1.0) }, {} }, false);
                auto verb = wet->getPluginCache().createNewPlugin (ReverbPlugin::xmlTypeName, {});
                t->pluginList.insertPlugin (verb, 0, nullptr);

                auto renderWith = [&] (double tail, const juce::String& name)
                {
                    Options o;
                    o.tailSeconds = tail;
                    o.dither = false;
                    auto dest = outDir.getChildFile (name + ".wav");
                    juce::StringArray results;
                    expect (runJob ({ { makeParameters (*wet, o, dest, {}), name } }, results), results.joinIntoString ("; "));
                    return readBack (dest);
                };

                auto without = renderWith (0.0, "tail_none");
                auto with = renderWith (2.0, "tail_two");
                logMessage ("No tail: " + juce::String (without.getNumSamples() / 48000.0, 2) + " s; 2 s tail: " + juce::String (with.getNumSamples() / 48000.0, 2) + " s");
                expectWithinAbsoluteError (without.getNumSamples() / 48000.0, 1.0, 0.02);
                expectGreaterThan (with.getNumSamples() / 48000.0, 1.5, "the file runs on past the song");

                if (with.getNumSamples() > 48000 * 1.2)
                    expectGreaterThan (with.getRMSLevel (0, (int) (48000 * 1.0), (int) (48000 * 0.2)), 0.0005f, "the reverb is still sounding after the song ends");
            }
        }

        void stemsThroughRouting (const juce::File& outDir)
        {
            using namespace ExportService;

            auto edit = te::test_utilities::createTestEdit (getEngine(), 0, te::Edit::forRendering);
            auto group = TrackOps::addFolderTrack (*edit, nullptr, true);
            group->setName ("Drums");
            auto inside = TrackOps::addAudioTrack (*edit, nullptr, group.get(), "Kick");
            edit->moveTrack (inside, te::TrackInsertPoint (group.get(), nullptr));
            inside->insertWaveClip ("k", writeSineFile ("stem_kick", 2.0, 0.25f, 100.0), { { te::TimePosition(), te::TimeDuration::fromSeconds (2.0) }, {} }, false);
            group->getVolumePlugin()->setVolumeDb (-6.0f);

            auto fx = TrackOps::addFxTrack (*edit, nullptr);
            fx->setName ("Reverb");
            auto verb = edit->getPluginCache().createNewPlugin (ReverbPlugin::xmlTypeName, {});
            fx->pluginList.insertPlugin (verb, 0, nullptr);
            MixerOps::addSend (*inside, MixerOps::getFxBuses (*edit).getFirst().number, true, -6.0f);

            auto render = [&] (const juce::Array<te::Track*>& tracks, const juce::String& name)
            {
                Options o;
                o.tailSeconds = 0.0;
                o.dither = false;
                auto dest = outDir.getChildFile (name + ".wav");
                juce::StringArray results;
                expect (runJob ({ { makeParameters (*edit, o, dest, tracks), name } }, results), name + ": " + results.joinIntoString ("; "));
                return readBack (dest);
            };

            beginTest ("Stems: a track inside a group");
            {
                auto stem = render ({ inside.get() }, "stem_inside_group");
                logMessage ("Stem of a track in a group: peak " + juce::String (peakDb (stem), 2) + " dBFS (the clip alone is -12.04)");
                expectWithinAbsoluteError (peakDb (stem), -12.04f, 0.1f, "the track before its group's fader, without the reverb it sends to");
            }

            beginTest ("Stems: an FX return carries what's sent to it, and only that");
            {
                auto stem = render ({ fx.get() }, "stem_fx");
                logMessage ("Stem of an FX return: peak " + juce::String (peakDb (stem), 2) + " dBFS");
                expectGreaterThan (peakDb (stem), -50.0f, "the reverb of what's sent to it");
                expectLessThan (peakDb (stem), -12.0f, "not the dry track as well");
                expect (! inside->isMuted (false), "the sending track is unmuted again afterwards");

                inside->setMute (true);
                auto muted = render ({ fx.get() }, "stem_fx_muted");
                inside->setMute (false);
                expectLessThan (peakDb (muted), -100.0f, "a track muted in the song sends nothing, as in the mix");
            }

            beginTest ("Stems: a compressor's sidechain still works in a stem");
            {
                auto sideEdit = te::test_utilities::createTestEdit (getEngine(), 2, te::Edit::forRendering);
                auto audio = te::getAudioTracks (*sideEdit);
                audio[0]->insertWaveClip ("sound", writeSineFile ("side_sound", 2.0, 0.5f), { { te::TimePosition(), te::TimeDuration::fromSeconds (2.0) }, {} }, false);
                audio[1]->insertWaveClip ("key", writeSineFile ("side_key", 2.0, 0.9f, 200.0), { { te::TimePosition(), te::TimeDuration::fromSeconds (2.0) }, {} }, false);

                auto compPlugin = sideEdit->getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});
                audio[0]->pluginList.insertPlugin (compPlugin, 0, nullptr);
                auto* comp = dynamic_cast<CompressorPlugin*> (compPlugin.get());
                comp->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-30.0f), juce::sendNotification);
                comp->ratio.setParameter (0.1f, juce::sendNotification);
                MixerOps::setSidechainSource (*comp, audio[1]);

                Options o;
                o.tailSeconds = 0.0;
                o.dither = false;
                auto dest = outDir.getChildFile ("stem_ducked.wav");
                juce::StringArray results;
                expect (runJob ({ { makeParameters (*sideEdit, o, dest, { audio[0] }), "Ducked" } }, results), results.joinIntoString ("; "));
                auto stem = readBack (dest);
                auto level = juce::Decibels::gainToDecibels (stem.getRMSLevel (0, 48000, 24000));
                logMessage ("Sidechained stem: " + juce::String (level, 1) + " dB RMS (the clip alone is -9 dB)");
                expectLessThan (level, -12.0f, "the sidechain ducks the stem");
                expect (! audio[1]->isMuted (false));
            }
        }
    };

    static ExportTests exportTests;
}
