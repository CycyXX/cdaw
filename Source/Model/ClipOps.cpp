#include "ClipOps.h"
#include <map>
#include <unordered_set>
#include "ProjectSession.h"
#include "TrackOps.h"

namespace cdaw::ClipOps
{
    static constexpr double minClipLength = 0.005;

    bool isImportableAudioFile (const juce::File& f)
    {
        return f.hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3;bwf");
    }

    bool isMidiFile (const juce::File& f)
    {
        return f.hasFileExtension ("mid;midi");
    }

    using Progress = std::function<bool (double fraction)>;   // return false to cancel

    /** MP3 and OGG can't be read randomly, so the engine would build a hidden proxy file in the
        background and stay silent until it's done. Decode them to a 24-bit WAV at import instead. */
    static juce::File decodeToWav (ProjectSession& session, te::Engine& engine, const juce::File& file, const Progress& progress)
    {
        auto dest = session.getAudioFolder().getChildFile ("Imported");
        dest.createDirectory();
        auto target = dest.getNonexistentChildFile (file.getFileNameWithoutExtension(), ".wav", false);

        std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file));
        if (reader == nullptr)
            return {};

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out (target.createOutputStream());
        if (out == nullptr)
            return {};

        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), reader->sampleRate, reader->numChannels, 24, {}, 0));
        if (writer == nullptr)
            return {};

        out.release();   // the writer owns the stream now

        const juce::int64 total = reader->lengthInSamples;
        constexpr int block = 1 << 16;
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, block);
        bool ok = true;

        for (juce::int64 pos = 0; pos < total && ok; pos += block)
        {
            auto n = (int) juce::jmin ((juce::int64) block, total - pos);
            reader->read (&buffer, 0, n, pos, true, reader->numChannels > 1);
            ok = writer->writeFromAudioSampleBuffer (buffer, 0, n);

            if (progress && ! progress ((double) (pos + n) / (double) juce::jmax ((juce::int64) 1, total)))
                ok = false;
        }

        writer.reset();

        if (! ok)
        {
            target.deleteFile();
            return {};
        }

        return target;
    }

    static juce::File copyIntoProjectIfNeeded (ProjectSession& session, const juce::File& file, bool copy)
    {
        if (! copy || file.isAChildOf (session.getProjectFolder()))
            return file;

        auto dest = session.getAudioFolder().getChildFile ("Imported");
        dest.createDirectory();
        auto target = dest.getChildFile (file.getFileName());

        // Re-use an identical copy rather than duplicating it.
        if (target.existsAsFile() && target.getSize() == file.getSize())
            return target;

        target = dest.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);
        return file.copyFileTo (target) ? target : file;
    }

    juce::Array<juce::File> prepareAudioFiles (ProjectSession& session, const juce::Array<juce::File>& files, bool copyIntoProject,
                                               const std::function<bool (const juce::String&, double)>& progress)
    {
        juce::Array<juce::File> prepared;
        auto& engine = session.getEdit().engine;
        int index = 0;

        for (auto& source : files)
        {
            ++index;

            if (! isImportableAudioFile (source))
                continue;

            auto report = [&] (double f) { return ! progress || progress (source.getFileName(), ((double) (index - 1) + f) / (double) files.size()); };

            if (source.hasFileExtension ("mp3;ogg"))
            {
                auto decoded = decodeToWav (session, engine, source, report);

                if (decoded == juce::File())
                {
                    juce::Logger::writeToLog ("Import failed or cancelled: " + source.getFullPathName());
                    continue;
                }

                prepared.add (decoded);
            }
            else
            {
                if (! report (0.0))
                    break;

                prepared.add (copyIntoProjectIfNeeded (session, source, copyIntoProject));
            }
        }

        return prepared;
    }

    juce::Array<te::Clip*> insertPreparedAudio (ProjectSession& session, const juce::Array<juce::File>& files,
                                                te::AudioTrack* track, te::TimePosition start)
    {
        auto& edit = session.getEdit();
        auto& sm = session.getSelectionManager();
        juce::Array<te::Clip*> result;
        te::Track* lastTrack = track;

        edit.getUndoManager().beginNewTransaction ("Import audio");

        for (auto& file : files)
        {
            te::AudioFile af (edit.engine, file);

            if (! af.isValid())
            {
                juce::Logger::writeToLog ("Import skipped (unreadable): " + file.getFullPathName());
                continue;
            }

            te::AudioTrack* target = nullptr;

            if (track != nullptr && result.isEmpty())
            {
                target = track;
            }
            else if (track != nullptr)
            {
                auto all = te::getAudioTracks (edit);
                auto idx = all.indexOf (dynamic_cast<te::AudioTrack*> (lastTrack));
                target = idx >= 0 && idx + 1 < all.size() ? all[idx + 1] : nullptr;
            }

            if (target == nullptr)
                target = TrackOps::addAudioTrack (edit, nullptr, lastTrack, file.getFileNameWithoutExtension()).get();

            if (target == nullptr)
                continue;

            auto length = te::TimeDuration::fromSeconds (af.getLength());
            auto range = te::TimeRange (start, length);
            clearRangeForClip (*target, range, {});

            if (auto clip = target->insertWaveClip (file.getFileNameWithoutExtension(), file, { range, {} }, false))
            {
                result.add (clip.get());
                juce::Logger::writeToLog ("Imported " + file.getFileName() + " at " + juce::String (start.inSeconds(), 2) + " s, "
                                          + juce::String (length.inSeconds(), 1) + " s long");
            }

            lastTrack = target;
        }

        if (! result.isEmpty())
        {
            sm.deselectAll();
            for (auto c : result)
                sm.addToSelection (c);
        }

        return result;
    }

    namespace
    {
        /** Non-blocking progress window for slow imports. Converts on a background thread,
            then inserts the clips on the message thread. Never runs a nested modal loop
            (doing that from a drop or timer callback re-delivers the event on Windows). */
        class ImportJob : public juce::DocumentWindow,
                          private juce::Thread,
                          private juce::Timer
        {
        public:
            ImportJob (ProjectSession& s, juce::Array<juce::File> f, te::AudioTrack* t, te::TimePosition st, bool copy)
                : juce::DocumentWindow ("Importing audio", juce::Colour (0xff23262b), 0, true),
                  juce::Thread ("CDAW import"),
                  session (s), files (std::move (f)), track (t), startTime (st), copyIntoProject (copy)
            {
                setUsingNativeTitleBar (true);
                content.setSize (440, 110);
                status.setBounds (12, 10, 416, 24);
                bar.setBounds (12, 40, 416, 22);
                cancel.setBounds (338, 72, 90, 26);
                cancel.onClick = [this] { signalThreadShouldExit(); };
                content.addAndMakeVisible (status);
                content.addAndMakeVisible (bar);
                content.addAndMakeVisible (cancel);
                setContentNonOwned (&content, true);
                setAlwaysOnTop (true);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);

                startThread();
                startTimerHz (15);
            }

            ~ImportJob() override
            {
                stopTimer();
                stopThread (10000);
            }

            static void launch (ProjectSession& s, juce::Array<juce::File> f, te::AudioTrack* t, te::TimePosition st, bool copy)
            {
                if (current != nullptr)
                {
                    s.getEdit().engine.getUIBehaviour().showWarningMessage ("An import is already running");
                    return;
                }

                current = std::make_unique<ImportJob> (s, std::move (f), t, st, copy);
            }

            static void cancelAndWait()   { current.reset(); }

        private:
            void run() override
            {
                auto result = prepareAudioFiles (session, files, copyIntoProject, [this] (const juce::String& name, double fraction)
                {
                    const juce::ScopedLock sl (lock);
                    message = "Converting " + name;
                    progress = fraction;
                    return ! threadShouldExit();
                });

                const juce::ScopedLock sl (lock);
                prepared = result;
                done = true;
            }

            void timerCallback() override
            {
                juce::String msg;
                bool finished;

                {
                    const juce::ScopedLock sl (lock);
                    msg = message;
                    progressValue = progress;
                    finished = done;
                }

                status.setText (msg, juce::dontSendNotification);

                if (! finished)
                    return;

                stopTimer();
                const bool cancelled = threadShouldExit();
                stopThread (1000);

                // The project may have been closed meanwhile.
                auto& ctxSession = session;
                juce::Array<te::Clip*> clips;

                if (! cancelled)
                    clips = insertPreparedAudio (ctxSession, prepared, dynamic_cast<te::AudioTrack*> (track.get()), startTime);

                ctxSession.getEdit().engine.getUIBehaviour().showInfoMessage (cancelled ? juce::String ("Import cancelled")
                                                                                        : "Imported " + juce::String (clips.size()) + " file(s)");

                juce::MessageManager::callAsync ([] { current.reset(); });
            }

            void closeButtonPressed() override  { signalThreadShouldExit(); }

            ProjectSession& session;
            juce::Array<juce::File> files, prepared;
            te::Track::Ptr track;
            te::TimePosition startTime;
            bool copyIntoProject;

            juce::CriticalSection lock;
            juce::String message { "Preparing..." };
            double progress = 0.0, progressValue = 0.0;
            bool done = false;

            juce::Component content;
            juce::Label status;
            juce::ProgressBar bar { progressValue };
            juce::TextButton cancel { "Cancel" };

            static inline std::unique_ptr<ImportJob> current;
        };
    }

    void cancelRunningImport()
    {
        ImportJob::cancelAndWait();
    }

    juce::Array<te::Clip*> importAudioFiles (ProjectSession& session, const juce::Array<juce::File>& files,
                                             te::AudioTrack* track, te::TimePosition start, bool copyIntoProject)
    {
        // Only compressed formats need real work; those import in the background with progress.
        bool slow = false;
        for (auto& f : files)
            slow = slow || f.hasFileExtension ("mp3;ogg");

        if (! slow)
            return insertPreparedAudio (session, prepareAudioFiles (session, files, copyIntoProject, {}), track, start);

        ImportJob::launch (session, files, track, start, copyIntoProject);
        return {};
    }

    juce::Array<te::Clip*> importAudioFilesNow (ProjectSession& session, const juce::Array<juce::File>& files,
                                                te::AudioTrack* track, te::TimePosition start, bool copyIntoProject)
    {
        return insertPreparedAudio (session, prepareAudioFiles (session, files, copyIntoProject, {}), track, start);
    }

    juce::Array<te::Clip*> getSelectedClips (te::SelectionManager& sm)
    {
        juce::Array<te::Clip*> clips;

        for (auto s : sm.getSelectedObjects())
            if (auto c = dynamic_cast<te::Clip*> (s))
                clips.add (c);

        return clips;
    }

    void clearRangeForClip (te::ClipTrack& track, te::TimeRange range, const juce::Array<te::Clip*>& exclude)
    {
        juce::ReferenceCountedArray<te::Clip> overlapping;
        const std::unordered_set<te::Clip*> excluded (exclude.begin(), exclude.end());

        for (auto c : track.getClips())
            if (c->getEditTimeRange().overlaps (range) && excluded.count (c) == 0)
                overlapping.add (c);

        for (auto c : overlapping)
            te::deleteRegionOfClip (*c, range);
    }

    juce::Array<te::Clip*> moveClips (te::Edit& edit, const juce::Array<te::Clip*>& clips, te::TimeDuration delta,
                                      int trackDelta, bool copy)
    {
        juce::Array<te::Clip*> moved;

        if (clips.isEmpty())
            return moved;

        // Don't move anything before zero.
        for (auto c : clips)
            if (c->getPosition().getStart() + delta < te::TimePosition())
                delta = te::TimePosition() - c->getPosition().getStart();

        auto tracks = TrackOps::getArrangeTracks (edit);

        for (auto c : clips)
        {
            te::Clip::Ptr clip = c;

            if (copy)
                clip = te::duplicateClip (*c);

            if (clip == nullptr)
                continue;

            if (trackDelta != 0)
            {
                auto idx = tracks.indexOf (c->getTrack());
                auto newIdx = idx + trackDelta;

                // Find the nearest clip track at or beyond the target index.
                while (juce::isPositiveAndBelow (newIdx, tracks.size()) && dynamic_cast<te::ClipTrack*> (tracks[newIdx]) == nullptr)
                    newIdx += trackDelta > 0 ? 1 : -1;

                if (auto destTrack = dynamic_cast<te::ClipTrack*> (tracks[newIdx]))
                    if (destTrack != clip->getClipTrack())
                        clip->moveTo (*destTrack);
            }

            auto pos = clip->getPosition();
            pos.time = pos.time + delta;
            clip->setPosition (pos);
            moved.add (clip.get());
        }

        // Moved clips replace whatever they now cover (Cubase-style "clip on top wins").
        for (auto c : moved)
            if (auto ct = c->getClipTrack())
                clearRangeForClip (*ct, c->getEditTimeRange(), moved);

        return moved;
    }

    void splitAt (te::Edit& edit, te::SelectionManager& sm, te::TimePosition time)
    {
        te::SelectableList toSplit;

        for (auto c : getSelectedClips (sm))
            toSplit.add (c);

        if (toSplit.isEmpty())
            for (auto ct : te::getClipTracks (edit))
                for (auto c : ct->getClips())
                    toSplit.add (c);

        edit.getUndoManager().beginNewTransaction ("Split");
        te::splitClips (toSplit, time);
    }

    void deleteClips (const juce::Array<te::Clip*>& clips)
    {
        juce::ReferenceCountedArray<te::Clip> keep;
        for (auto c : clips)
            keep.add (c);

        for (auto c : keep)
            c->removeFromParent();
    }

    juce::Array<te::Clip*> duplicateClips (te::Edit& edit, const juce::Array<te::Clip*>& clips)
    {
        if (clips.isEmpty())
            return {};

        auto start = clips.getFirst()->getPosition().getStart();
        auto end = clips.getFirst()->getPosition().getEnd();

        for (auto c : clips)
        {
            start = juce::jmin (start, c->getPosition().getStart());
            end = juce::jmax (end, c->getPosition().getEnd());
        }

        return moveClips (edit, clips, end - start, 0, true);
    }

    void setMuted (const juce::Array<te::Clip*>& clips, bool muted)
    {
        for (auto c : clips)
        {
            if (auto ac = dynamic_cast<te::AudioClipBase*> (c))
                ac->setMuted (muted);
            else if (auto mc = dynamic_cast<te::MidiClip*> (c))
                mc->setMuted (muted);
        }
    }

    void trimStart (te::Clip& clip, te::TimePosition t)
    {
        auto end = clip.getPosition().getEnd();
        t = juce::jmin (t, end - te::TimeDuration::fromSeconds (minClipLength));
        t = juce::jmax (t, te::TimePosition());

        // Can't extend before the start of the source material.
        auto pos = clip.getPosition();
        auto earliest = pos.getStart() - pos.getOffset();

        if (clip.usesSourceFile())
            t = juce::jmax (t, earliest);

        clip.setStart (t, true, false);
    }

    void trimEnd (te::Clip& clip, te::TimePosition t)
    {
        auto start = clip.getPosition().getStart();
        t = juce::jmax (t, start + te::TimeDuration::fromSeconds (minClipLength));
        clip.setEnd (t, true);
    }

    //==============================================================================
    bool createCrossfade (te::AudioClipBase& left, te::AudioClipBase& right, te::TimeDuration length)
    {
        auto* track = left.getClipTrack();

        if (track == nullptr || track != right.getClipTrack() || &left == &right)
            return false;

        const auto tolerance = te::TimeDuration::fromSeconds (0.002);
        const auto minimumKept = te::TimeDuration::fromSeconds (minClipLength);
        auto lr = left.getEditTimeRange();
        auto rr = right.getEditTimeRange();

        // Must be in order, touching or overlapping, and neither inside the other.
        if (lr.getStart() >= rr.getStart() || rr.getStart() > lr.getEnd() + tolerance || rr.getEnd() <= lr.getEnd())
            return false;

        if (rr.getStart() >= lr.getEnd() - tolerance)
        {
            // Touching: extend each clip into the other by half the length, as far as its audio allows.
            const auto half = te::TimeDuration::fromSeconds (length.inSeconds() * 0.5);
            const auto boundary = rr.getStart();

            auto rightEarliest = boundary - right.getPosition().getOffset();
            auto newRightStart = juce::jmax (boundary - half, rightEarliest, lr.getStart() + minimumKept);
            right.setStart (newRightStart, true, false);

            auto leftEnd = lr.getEnd();
            auto newLeftEnd = juce::jmin (leftEnd + half, rr.getEnd() - minimumKept);

            if (! left.isLooping())
            {
                auto sourceSeconds = left.getSourceLength().inSeconds() / left.getSpeedRatio();
                auto used = (left.getPosition().getOffset() + left.getPosition().getLength()).inSeconds();
                newLeftEnd = juce::jmin (newLeftEnd, leftEnd + te::TimeDuration::fromSeconds (juce::jmax (0.0, sourceSeconds - used)));
            }

            left.setEnd (newLeftEnd, true);
        }

        auto overlap = te::TimeRange (right.getPosition().getStart(), left.getPosition().getEnd());

        if (overlap.getLength() < te::TimeDuration::fromSeconds (0.001))
            return false;

        // Equal-power: the convex fade-in (sine) mirrored by the convex fade-out (cosine).
        left.setFadeOut (overlap.getLength());
        left.setFadeOutType (te::AudioFadeCurve::convex);
        right.setFadeIn (overlap.getLength());
        right.setFadeInType (te::AudioFadeCurve::convex);
        return true;
    }

    int crossfadeClips (const juce::Array<te::Clip*>& clips, te::TimeDuration length)
    {
        auto byStart = [] (te::Clip* a, te::Clip* b) { return a->getPosition().getStart() < b->getPosition().getStart(); };
        int made = 0;

        // One clip: crossfade it with whatever touches it on its track.
        if (clips.size() == 1)
        {
            auto* clip = dynamic_cast<te::AudioClipBase*> (clips.getFirst());
            auto* track = clips.getFirst()->getClipTrack();

            if (clip == nullptr || track == nullptr)
                return 0;

            juce::ReferenceCountedArray<te::Clip> neighbours;
            for (auto c : track->getClips())
                neighbours.add (c);

            for (auto c : neighbours)
                if (auto other = dynamic_cast<te::AudioClipBase*> (c); other != nullptr && other != clip)
                {
                    auto [left, right] = byStart (other, clip) ? std::pair { other, clip } : std::pair { clip, other };
                    if (createCrossfade (*left, *right, length))
                        ++made;
                }

            return made;
        }

        // Several: each touching pair on the same track.
        std::map<te::ClipTrack*, juce::Array<te::AudioClipBase*>> perTrack;

        for (auto c : clips)
            if (auto ac = dynamic_cast<te::AudioClipBase*> (c))
                if (auto t = c->getClipTrack())
                    perTrack[t].add (ac);

        for (auto& [track, list] : perTrack)
        {
            std::sort (list.begin(), list.end(), [&] (te::AudioClipBase* a, te::AudioClipBase* b) { return byStart (a, b); });

            for (int i = 0; i + 1 < list.size(); ++i)
                if (createCrossfade (*list[i], *list[i + 1], length))
                    ++made;
        }

        return made;
    }

    bool canToggleLoop (te::Clip& clip)
    {
        return dynamic_cast<te::MidiClip*> (&clip) != nullptr;
    }

    void setLooping (te::Clip& clip, bool shouldLoop)
    {
        auto mc = dynamic_cast<te::MidiClip*> (&clip);

        if (mc == nullptr || shouldLoop == mc->isLooping())
            return;

        if (shouldLoop)
        {
            // Loop exactly what the clip shows now; the offset moves into the loop start.
            auto start = te::BeatPosition() + mc->getOffsetInBeats();
            mc->setLoopRangeBeats ({ start, start + mc->getLengthInBeats() });
            mc->setOffset ({});
        }
        else
        {
            mc->disableLooping();
        }
    }

    juce::String describeClip (te::Clip& c)
    {
        auto pos = c.getPosition();
        return c.getName() + "  (" + juce::String (pos.getStart().inSeconds(), 3) + " s, "
               + juce::String (pos.getLength().inSeconds(), 3) + " s)";
    }
}
