#include "ExportService.h"

namespace cdaw::ExportService
{
    juce::String getExtension (Format f)
    {
        switch (f)
        {
            case Format::wav:  return ".wav";
            case Format::flac: return ".flac";
            case Format::ogg:  return ".ogg";
            case Format::mp3:  return ".mp3";
        }

        return ".wav";
    }

    static juce::AudioFormat* getAudioFormat (te::Engine& engine, Format f)
    {
        auto& afm = engine.getAudioFileFormatManager();

        switch (f)
        {
            case Format::wav:  return afm.getWavFormat();
            case Format::flac: return afm.getFlacFormat();
            case Format::ogg:  return afm.getOggFormat();
            case Format::mp3:  return afm.getLameFormat();
        }

        return afm.getWavFormat();
    }

    bool isFormatAvailable (te::Engine& engine, Format f)
    {
        return getAudioFormat (engine, f) != nullptr;
    }

    te::TimeRange getRenderRange (te::Edit& edit, const Options& o)
    {
        if (o.range == Range::loop)
        {
            auto loop = edit.getTransport().getLoopRange();
            if (! loop.isEmpty())
                return loop;
        }

        if (o.range == Range::custom)
            return o.customRange;       // empty when nothing was chosen: nothing to export

        return { te::TimePosition(), te::TimePosition() + edit.getLength() };
    }

    te::Renderer::Parameters makeParameters (te::Edit& edit, const Options& o, const juce::File& dest,
                                             const juce::Array<te::Track*>& tracks)
    {
        te::Renderer::Parameters p (edit);
        p.destFile = dest;
        p.audioFormat = getAudioFormat (edit.engine, o.format);
        p.sampleRateForAudio = o.sampleRate;
        p.blockSizeForAudio = 512;
        p.time = getRenderRange (edit, o);
        p.endAllowance = te::TimeDuration::fromSeconds (o.tailSeconds);
        p.usePlugins = true;
        p.useMasterPlugins = tracks.isEmpty();     // stems are taken pre-master
        p.canRenderInMono = false;
        p.realTimeRender = false;
        p.checkNodesForAudio = false;

        // Float WAV at 32 bit; FLAC tops out at 24; OGG/MP3 ignore bit depth.
        p.bitDepth = o.format == Format::flac ? juce::jmin (24, o.bitDepth) : o.bitDepth;
        p.ditheringEnabled = o.dither && p.bitDepth < 32 && (o.format == Format::wav || o.format == Format::flac);

        if (o.format == Format::mp3 || o.format == Format::ogg)
        {
            auto qualities = p.audioFormat != nullptr ? p.audioFormat->getQualityOptions() : juce::StringArray();
            p.quality = o.mp3QualityIndex >= 0 ? o.mp3QualityIndex : juce::jmax (0, qualities.size() - 1);
        }

        switch (o.normalise)
        {
            case Normalise::peak:
                p.shouldNormalise = true;
                p.normaliseToLevelDb = o.normaliseLevel;
                break;
            case Normalise::lufs:
                p.shouldNormaliseByLUFS = true;
                p.normaliseToLevelDb = o.normaliseLevel;
                p.limitTruePeak = true;
                p.truePeakCeilingDb = o.truePeakCeiling;
                break;
            case Normalise::none:
                break;
        }

        if (! tracks.isEmpty())
        {
            p.tracksToDo = te::toBitSet (tracks);

            // What the stem needs without being part of it: the tracks sending to an FX return, the
            // source of a sidechain. They play through the render unheard (Job mutes them while it
            // runs). A track muted in the song stays out, as it does in the mix.
            juce::Array<te::EditItemID> ids;

            for (auto* t : tracks)
                ids.add (t->itemID);

            auto allTracks = te::getAllTracks (edit);

            for (auto id : te::findStemSourceTracks (edit, ids))
            {
                if (auto* source = te::findTrackForID (edit, id); source != nullptr && ! source->isMuted (true))
                {
                    p.tracksToDo.setBit (allTracks.indexOf (source));
                    p.tracksToProcessWhileMuted.add (id);
                }
            }
        }

        auto meta = edit.getEditMetadata();
        if (meta.title.isNotEmpty())  p.metadata.set ("title", meta.title);
        if (meta.artist.isNotEmpty()) p.metadata.set ("artist", meta.artist);

        return p;
    }

    bool exportMidi (te::Edit& edit, const juce::Array<te::MidiClip*>& clips, const juce::File& dest)
    {
        juce::Array<te::MidiClip*> toExport = clips;

        if (toExport.isEmpty())
            for (auto t : te::getAudioTracks (edit))
                for (auto c : t->getClips())
                    if (auto mc = dynamic_cast<te::MidiClip*> (c))
                        toExport.add (mc);

        if (toExport.isEmpty())
            return false;

        constexpr int ppq = 960;
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);

        // Track 0: tempo and time signatures.
        juce::MidiMessageSequence tempoTrack;
        auto& ts = edit.tempoSequence;

        for (int i = 0; i < ts.getNumTempos(); ++i)
        {
            auto* tempo = ts.getTempo (i);
            auto beat = tempo->getStartBeat().inBeats();
            tempoTrack.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / tempo->getBpm())), beat * ppq);
        }

        for (int i = 0; i < ts.getNumTimeSigs(); ++i)
        {
            auto* sig = ts.getTimeSig (i);
            auto beat = sig->getStartBeat().inBeats();
            tempoTrack.addEvent (juce::MidiMessage::timeSignatureMetaEvent (sig->numerator.get(), sig->denominator.get()), beat * ppq);
        }

        file.addTrack (tempoTrack);

        // One MIDI track per CDAW track.
        std::map<te::Track*, juce::MidiMessageSequence> perTrack;

        for (auto mc : toExport)
        {
            if (mc->isMuted())
                continue;

            // Beats are quarter notes here (lengthOfOneBeatDependsOnTimeSignature aside, 4/4 material is
            // the norm), counted from the clip's start. The list holds more than the clip plays (a take
            // in a comp holds the whole pass): what's outside the clip stays out, what overlaps its
            // edges is cut there, as it sounds.
            auto seq = mc->getSequence().exportToPlaybackMidiSequence (*mc, te::MidiList::TimeBase::beats, false);
            seq.updateMatchedPairs();

            const auto clipStart = mc->getStartBeat().inBeats();
            const auto clipLength = mc->getLengthInBeats().inBeats();
            auto& out = perTrack[mc->getTrack()];

            auto add = [&] (juce::MidiMessage msg, double beat)
            {
                msg.setTimeStamp ((clipStart + beat) * ppq);
                out.addEvent (msg);
            };

            for (auto* ev : seq)
            {
                const auto time = ev->message.getTimeStamp();

                if (ev->message.isNoteOn())
                {
                    const auto end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : clipLength;

                    if (end > 0.0 && time < clipLength)
                    {
                        add (ev->message, juce::jmax (0.0, time));
                        add (juce::MidiMessage::noteOff (ev->message.getChannel(), ev->message.getNoteNumber()), juce::jmin (end, clipLength));
                    }
                }
                else if (! ev->message.isNoteOff() && time < clipLength)
                {
                    add (ev->message, juce::jmax (0.0, time));
                }
            }
        }

        for (auto& [track, seq] : perTrack)
        {
            juce::MidiMessageSequence named;
            named.addEvent (juce::MidiMessage::textMetaEvent (3, track != nullptr ? track->getName() : juce::String ("MIDI")), 0.0);
            named.addSequence (seq, 0.0);
            named.updateMatchedPairs();
            file.addTrack (named);
        }

        dest.deleteFile();
        juce::FileOutputStream out (dest);
        return out.openedOk() && file.writeTo (out, 1);
    }

    //==============================================================================
    Job::Job (std::vector<Item> i, std::function<void (bool, juce::StringArray)> done)
        : items (std::move (i)), onFinished (std::move (done))
    {
        if (! items.empty() && items.front().params.edit != nullptr)
            saveInhibitor = std::make_unique<te::Edit::SaveInhibitor> (*items.front().params.edit);

        startNext();
        startTimerHz (10);
    }

    Job::~Job()
    {
        *alive = false;
        stopTimer();
        handle.reset();
        sourceMuter.reset();
    }

    juce::String Job::getCurrentDescription() const
    {
        return current < items.size() ? items[current].description : juce::String();
    }

    void Job::cancel()
    {
        cancelled = true;

        if (handle != nullptr)
            handle->cancel();
    }

    void Job::startNext()
    {
        if (cancelled || failed || current >= items.size())
        {
            finished = true;
            stopTimer();
            saveInhibitor.reset();

            if (onFinished)
                onFinished (! failed && ! cancelled, results);

            return;
        }

        auto& item = items[current];
        item.params.destFile.getParentDirectory().createDirectory();
        item.params.destFile.deleteFile();

        if (! item.params.tracksToProcessWhileMuted.isEmpty())
            sourceMuter = std::make_unique<te::ScopedTrackMuter> (*item.params.edit, item.params.tracksToProcessWhileMuted);

        std::weak_ptr<bool> weakAlive = alive;

        handle = te::EditRenderer::render (item.params, [this, weakAlive] (tl::expected<juce::File, std::string> result)
        {
            if (auto a = weakAlive.lock(); a == nullptr || ! *a)
                return;

            if (result)
            {
                results.add (result->getFullPathName());
            }
            else if (! cancelled)
            {
                failed = true;
                results.add ("FAILED: " + items[current].description + " - " + juce::String (result.error()));
            }

            ++current;

            // Start the next render once this callback has returned.
            juce::MessageManager::callAsync ([this, weakAlive]
            {
                if (auto a = weakAlive.lock(); a != nullptr && *a)
                {
                    handle.reset();
                    sourceMuter.reset();
                    startNext();
                }
            });
        });
    }

    void Job::timerCallback()
    {
        if (items.empty())
            return;

        auto part = handle != nullptr ? handle->getProgress() : 0.0f;
        progress = juce::jlimit (0.0f, 1.0f, ((float) current + part) / (float) items.size());
    }
}
