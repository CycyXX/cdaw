#include "TestEngine.h"
#include "Model/DawProject.h"
#include "Plugins/EqPlugin.h"
#include "Model/MixerOps.h"
#include "Model/TempoOps.h"
#include "Model/TrackOps.h"

namespace cdaw::test
{
    struct DawProjectTests : public juce::UnitTest
    {
        DawProjectTests() : juce::UnitTest ("DAWproject import / export", "CDAW") {}

        static te::AudioTrack* findTrack (te::Edit& edit, const juce::String& name)
        {
            for (auto t : te::getAudioTracks (edit))
                if (t->getName() == name)
                    return t;
            return nullptr;
        }

        static te::FolderTrack* findFolder (te::Edit& edit, const juce::String& name)
        {
            for (auto t : te::getAllTracks (edit))
                if (auto f = dynamic_cast<te::FolderTrack*> (t); f != nullptr && f->getName() == name)
                    return f;
            return nullptr;
        }

        static std::unique_ptr<juce::XmlElement> readProjectXml (const juce::File& file)
        {
            juce::ZipFile zip (file);
            if (auto* entry = zip.getEntry ("project.xml"))
                if (auto stream = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (*entry)))
                    return juce::parseXML (stream->readEntireStreamAsString());
            return {};
        }

        void runTest() override
        {
            auto& engine = getEngine();
            auto dir = getTestDir().getChildFile ("dawproject");
            dir.deleteRecursively();
            dir.createDirectory();

            beginTest ("A project survives export and re-import");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 0);
                auto& ts = edit->tempoSequence;
                ts.getTempo (0)->setBpm (100.0);
                ts.insertTempo (te::BeatPosition::fromBeats (32.0), 140.0, 1.0f);
                ts.insertTimeSig (te::BeatPosition::fromBeats (16.0))->setStringTimeSig ("3/4");

                auto beat = [&] (double b) { return ts.toTime (te::BeatPosition::fromBeats (b)); };

                // Group "Drums" holding an audio track; an instrument track; an FX return.
                auto group = TrackOps::addFolderTrack (*edit, nullptr, true);
                group->setName ("Drums");
                auto kick = TrackOps::addAudioTrack (*edit, nullptr, nullptr, "Kick");
                edit->moveTrack (kick, te::TrackInsertPoint (group.get(), nullptr));
                kick->getVolumePlugin()->setVolumeDb (-6.0f);
                kick->getVolumePlugin()->setPan (-0.5f);

                auto wav = writeSineFile ("dp_kick", 3.0, 0.5f, 100.0);
                auto clip = kick->insertWaveClip ("Kick 1", wav, { { beat (4.0), beat (8.0) }, te::TimeDuration::fromSeconds (0.25) }, false);
                clip->setFadeIn (te::TimeDuration::fromSeconds (0.1));
                clip->setFadeOut (te::TimeDuration::fromSeconds (0.2));

                auto keys = TrackOps::addInstrumentTrack (*edit, nullptr, nullptr);
                keys->setName ("Keys");
                auto mc = keys->insertMIDIClip ({ beat (0.0), beat (8.0) }, nullptr);
                mc->getSequence().addNote (60, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
                mc->getSequence().addNote (64, te::BeatPosition::fromBeats (2.0), te::BeatDuration::fromBeats (1.0), 64, 0, nullptr);
                mc->setLoopRangeBeats ({ te::BeatPosition::fromBeats (0.0), te::BeatPosition::fromBeats (4.0) });

                auto fx = TrackOps::addFxTrack (*edit, nullptr);
                fx->setName ("Reverb");
                auto bus = MixerOps::getFxBuses (*edit).getFirst().number;
                MixerOps::addSend (*kick, bus, false, -12.0f);

                auto comp = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
                kick->pluginList.insertPlugin (comp, 0, nullptr);
                auto* c = dynamic_cast<te::CompressorPlugin*> (comp.get());
                c->thresholdGain.setParameter (0.25f, juce::sendNotification);
                c->ratio.setParameter (0.25f, juce::sendNotification);

                auto eq = edit->getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                keys->pluginList.insertPlugin (eq, 0, nullptr);
                {
                    auto* e = dynamic_cast<EqPlugin*> (eq.get());
                    e->setType (2, EqMath::BandType::peak);
                    e->getBand (2).gain.setParameter (4.5f, juce::sendNotification);
                    e->getBand (2).freq.setParameter (2000.0f, juce::sendNotification);
                    e->setOn (2, true);
                    e->setType (0, EqMath::BandType::lowCut);
                    e->setSlope (0, 3);
                    e->setOn (0, true);
                }

                auto& volCurve = kick->getVolumePlugin()->volParam->getCurve();
                volCurve.addPoint (beat (0.0), te::decibelsToVolumeFaderPosition (-20.0f), 0.0f, nullptr);
                volCurve.addPoint (beat (8.0), te::decibelsToVolumeFaderPosition (0.0f), 0.0f, nullptr);

                TempoOps::addMarker (*edit, beat (16.0), "Chorus");

                auto file = dir.getChildFile ("roundtrip.dawproject");
                DawProject::Report out;
                auto result = DawProject::exportTo (*edit, file, out);
                expect (result.wasOk(), result.getErrorMessage());
                logMessage ("Export: " + out.summary());

                juce::ZipFile zip (file);
                expect (zip.getEntry ("project.xml") != nullptr && zip.getEntry ("metadata.xml") != nullptr);
                expect (zip.getEntry ("audio/dp_kick.wav") != nullptr, "the audio is embedded");

                auto xml = readProjectXml (file);
                expect (xml != nullptr && xml->hasTagName ("Project"));
                expectEquals (xml->getChildByName ("Transport")->getChildByName ("Tempo")->getDoubleAttribute ("value"), 100.0);

                // Re-import into a different edit.
                auto back = te::test_utilities::createTestEdit (engine, 1);
                DawProject::Report in;
                result = DawProject::importInto (*back, file, dir.getChildFile ("media"), in);
                expect (result.wasOk(), result.getErrorMessage());
                logMessage ("Import: " + in.summary());

                auto& ts2 = back->tempoSequence;
                expectWithinAbsoluteError (ts2.getTempo (0)->getBpm(), 100.0, 1e-6);
                expectEquals (ts2.getNumTempos(), 2);
                expectWithinAbsoluteError (ts2.getTempo (1)->getBpm(), 140.0, 1e-6);
                expectEquals (ts2.getNumTimeSigs(), 2);
                expectEquals (ts2.getTimeSig (1)->numerator.get(), 3);

                auto beat2 = [&] (double b) { return ts2.toTime (te::BeatPosition::fromBeats (b)); };

                auto drums = findFolder (*back, "Drums");
                expect (drums != nullptr && drums->isSubmixFolder(), "group comes back as a group");

                auto kick2 = findTrack (*back, "Kick");
                expect (kick2 != nullptr);
                expect (kick2 != nullptr && kick2->getParentFolderTrack() == drums, "Kick is inside the group");
                expectWithinAbsoluteError (kick2->getVolumePlugin()->getVolumeDb(), -6.0f, 0.01f);
                expectWithinAbsoluteError (kick2->getVolumePlugin()->getPan(), -0.5f, 0.001f);

                auto clips = kick2->getClips();
                expectEquals (clips.size(), 1);
                if (auto wc = dynamic_cast<te::WaveAudioClip*> (clips.getFirst()))
                {
                    expectWithinAbsoluteError (wc->getPosition().getStart().inSeconds(), beat2 (4.0).inSeconds(), 1e-6);
                    expectWithinAbsoluteError (wc->getPosition().getEnd().inSeconds(), beat2 (8.0).inSeconds(), 1e-6);
                    expectWithinAbsoluteError (wc->getPosition().getOffset().inSeconds(), 0.25, 1e-6);
                    expectWithinAbsoluteError (wc->getFadeIn().inSeconds(), 0.1, 1e-6);
                    expectWithinAbsoluteError (wc->getFadeOut().inSeconds(), 0.2, 1e-6);
                    expect (wc->getAudioFile().getFile().isAChildOf (dir.getChildFile ("media")), "audio extracted into the media folder");
                    expect (wc->getAudioFile().getFile().existsAsFile());
                }

                // Volume automation, back in fader positions.
                auto& curve2 = kick2->getVolumePlugin()->volParam->getCurve();
                expectEquals (curve2.getNumPoints(), 2);
                expectWithinAbsoluteError (te::volumeFaderPositionToDB (curve2.getPointValue (0)), -20.0f, 0.05f);

                // Compressor settings.
                te::CompressorPlugin* comp2 = nullptr;
                for (auto p : kick2->pluginList.getPlugins())
                    if (auto cp = dynamic_cast<te::CompressorPlugin*> (p))
                        comp2 = cp;
                expect (comp2 != nullptr, "compressor comes back");
                if (comp2 != nullptr)
                {
                    expectWithinAbsoluteError (comp2->thresholdGain.getCurrentValue(), 0.25f, 0.001f);
                    expectWithinAbsoluteError (comp2->ratio.getCurrentValue(), 0.25f, 0.001f);
                }

                // Send to the FX return.
                auto sends = MixerOps::getSends (*kick2);
                expectEquals (sends.size(), 1);
                if (! sends.isEmpty())
                {
                    expectWithinAbsoluteError (sends[0]->getGainDb(), -12.0f, 0.01f);
                    expect (! MixerOps::isPostFader (*sends[0]), "pre-fader kept");
                    auto fx2 = findTrack (*back, "Reverb");
                    expect (fx2 != nullptr && TrackOps::getKind (*fx2) == TrackKind::fx);
                    bool returnsBus = false;
                    for (auto p : fx2->pluginList.getPlugins())
                        if (auto ret = dynamic_cast<te::AuxReturnPlugin*> (p))
                            returnsBus = ret->busNumber.get() == sends[0]->getBusNumber();
                    expect (returnsBus, "send goes to the Reverb track's bus");
                }

                // MIDI: notes, loop and instrument track type.
                auto keys2 = findTrack (*back, "Keys");
                expect (keys2 != nullptr && TrackOps::getKind (*keys2) == TrackKind::instrument);
                if (keys2 != nullptr && ! keys2->getClips().isEmpty())
                {
                    auto mc2 = dynamic_cast<te::MidiClip*> (keys2->getClips().getFirst());
                    expect (mc2 != nullptr);
                    auto notes = mc2->getSequence().getNotes();
                    expectEquals (notes.size(), 2);
                    expectEquals (notes[0]->getNoteNumber(), 60);
                    expectWithinAbsoluteError (notes[0]->getStartBeat().inBeats(), 1.0, 1e-6);
                    expectEquals (notes[1]->getVelocity(), 64);
                    expect (mc2->isLooping());
                    expectWithinAbsoluteError (mc2->getLoopLengthBeats().inBeats(), 4.0, 1e-6);

                    EqPlugin* eqBack = nullptr;
                    for (auto p : keys2->pluginList.getPlugins())
                        if (auto e = dynamic_cast<EqPlugin*> (p))
                            eqBack = e;
                    expect (eqBack != nullptr, "EQ comes back");

                    if (eqBack != nullptr)
                    {
                        expect (eqBack->isOn (2) && ! eqBack->isOn (1), "band on/off kept");
                        expectWithinAbsoluteError (eqBack->getBand (2).gain.getCurrentValue(), 4.5f, 0.01f);
                        expectWithinAbsoluteError (eqBack->getBand (2).freq.getCurrentValue(), 2000.0f, 0.5f);
                        expect (eqBack->getType (0) == EqMath::BandType::lowCut && eqBack->getSlope (0) == 3, "48 dB/oct low cut kept");
                    }
                }

                auto markers = TempoOps::getMarkers (*back);
                expectEquals (markers.size(), 1);
                if (! markers.isEmpty())
                {
                    expectEquals (markers[0]->getName(), juce::String ("Chorus"));
                    expectWithinAbsoluteError (markers[0]->getPosition().getStart().inSeconds(), beat2 (16.0).inSeconds(), 1e-6);
                }
            }

            beginTest ("Bitwig-style nested clips, seconds and warps import");
            {
                auto wav = writeSineFile ("dp_ext", 4.0, 0.3f);
                auto file = dir.getChildFile ("bitwig.dawproject");

                juce::String xml;
                xml << R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Application name="Bitwig Studio" version="5.0"/>
  <Transport><Tempo unit="bpm" value="120" id="t0"/><TimeSignature numerator="4" denominator="4" id="t1"/></Transport>
  <Structure>
    <Track contentType="audio" loaded="true" id="tr1" name="Vox" color="#ff8800">
      <Channel audioChannels="2" destination="mch" role="regular" solo="false" id="ch1">
        <Devices><Vst2Plugin deviceName="OldVerb" deviceRole="audioFX" id="d1"/></Devices>
        <Mute value="false" id="m1"/>
        <Pan unit="normalized" value="0.75" min="0" max="1" id="p1"/>
        <Volume unit="linear" value="0.5" min="0" max="2" id="v1"/>
      </Channel>
    </Track>
    <Track contentType="audio notes" loaded="true" id="trm" name="Master">
      <Channel audioChannels="2" role="master" solo="false" id="mch"><Volume unit="linear" value="1" id="mv"/></Channel>
    </Track>
  </Structure>
  <Arrangement id="a">
    <Lanes timeUnit="beats" id="l0">
      <Lanes track="tr1" id="l1">
        <Clips id="c0">
          <Clip time="4" duration="4" playStart="0" name="Take">
            <Clips id="c1">
              <Clip time="0" duration="8" contentTimeUnit="seconds" playStart="1" fadeTimeUnit="seconds" fadeInTime="0.05" fadeOutTime="0">
                <Warps contentTimeUnit="seconds" timeUnit="seconds" id="w">
                  <Audio channels="2" sampleRate="48000" duration="4" algorithm="stretch" id="au">
                    <File path=")" << wav.getFileName() << R"(" external="true"/>
                  </Audio>
                  <Warp time="0" contentTime="0"/>
                  <Warp time="4" contentTime="4"/>
                </Warps>
              </Clip>
            </Clips>
          </Clip>
        </Clips>
        <Points unit="linear" id="pts"><Target parameter="v1"/>
          <RealPoint time="0" value="1" interpolation="hold"/>
          <RealPoint time="8" value="0.5" interpolation="linear"/>
        </Points>
      </Lanes>
    </Lanes>
  </Arrangement>
</Project>)";

                {
                    juce::ZipFile::Builder zip;
                    zip.addEntry (std::make_unique<juce::MemoryInputStream> (xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true), 6, "project.xml", juce::Time::getCurrentTime());
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    zip.writeToStream (out, nullptr);
                }

                // The external file is resolved relative to the .dawproject.
                wav.copyFileTo (dir.getChildFile (wav.getFileName()));

                auto edit = te::test_utilities::createTestEdit (engine, 2);
                DawProject::Report report;
                auto result = DawProject::importInto (*edit, file, dir.getChildFile ("media2"), report);
                expect (result.wasOk(), result.getErrorMessage());
                logMessage ("Import: " + report.summary());

                expectEquals (te::getAudioTracks (*edit).size(), 1, "the edit's old tracks were replaced");
                auto vox = findTrack (*edit, "Vox");
                expect (vox != nullptr);
                if (vox == nullptr)
                    return;

                expectWithinAbsoluteError (vox->getVolumePlugin()->getVolumeDb(), -6.02f, 0.01f);
                expectWithinAbsoluteError (vox->getVolumePlugin()->getPan(), 0.5f, 0.001f);
                expect (report.notes.joinIntoString ("\n").contains ("VST2"), "unsupported plugin is reported");

                // Outer clip at beat 4..8 (2 s .. 4 s at 120 bpm); inner content starts 1 s into the file.
                auto clips = vox->getClips();
                expectEquals (clips.size(), 1);
                if (auto wc = dynamic_cast<te::WaveAudioClip*> (clips.getFirst()))
                {
                    expectWithinAbsoluteError (wc->getPosition().getStart().inSeconds(), 2.0, 1e-6);
                    expectWithinAbsoluteError (wc->getPosition().getEnd().inSeconds(), 4.0, 1e-6);
                    expectWithinAbsoluteError (wc->getPosition().getOffset().inSeconds(), 1.0, 1e-6);
                    expectWithinAbsoluteError (wc->getFadeIn().inSeconds(), 0.05, 1e-6);
                }

                // Hold point: flat until just before the next point.
                auto& curve = vox->getVolumePlugin()->volParam->getCurve();
                expectEquals (curve.getNumPoints(), 3);
            }

            beginTest ("A file that isn't a DAWproject fails cleanly");
            {
                auto bogus = dir.getChildFile ("bogus.dawproject");
                bogus.replaceWithText ("not a zip");
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                DawProject::Report report;
                expect (DawProject::importInto (*edit, bogus, dir, report).failed());
                expectEquals (te::getAudioTracks (*edit).size(), 1, "edit untouched");
            }
        }
    };

    static DawProjectTests dawProjectTests;
}
