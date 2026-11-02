#include "CompOps.h"
#include "TrackOps.h"
#include <algorithm>
#include <map>
#include <set>

namespace cdaw::CompOps
{
    namespace
    {
        // Stored on each take: a TAKE tree for audio, a SEQUENCE tree for MIDI.
        const juce::Identifier rowID ("cdawTakeRow");       // the lane it shows in
        const juce::Identifier keyID ("cdawTakeKey");       // MIDI: the same take in every piece holding a copy
        const juce::Identifier shiftID ("cdawTakeShift");   // its start against the clip's other takes: seconds (audio), beats (MIDI)
        const juce::Identifier startID ("cdawTakeStart");   // MIDI: the recorded part of its time line, in beats
        const juce::Identifier endID ("cdawTakeEnd");

        constexpr double touchingSeconds = 0.001;

        struct Take
        {
            juce::ValueTree state;
            int index = 0;      // MIDI: its place in the clip's list of sequences
            int row = 0;
            juce::String key;
            juce::File file;
            te::MidiList* list = nullptr;
            double shift = 0.0;
        };

        std::vector<Take> readTakes (te::Clip& c)
        {
            std::vector<Take> takes;

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
            {
                std::set<juce::String> seen;

                // Tracktion adds the first pass both first and last; keeping first occurrences
                // gives recording order: pass 1, pass 2, ...
                for (auto state : wc->state.getChildWithName (te::IDs::TAKES))
                {
                    auto file = te::SourceFileReference::findFileFromString (wc->edit, state.getProperty (te::IDs::source).toString());

                    if (file != juce::File() && seen.insert (file.getFullPathName()).second)
                    {
                        Take t;
                        t.state = state;
                        t.row = state.getProperty (rowID, (int) takes.size());
                        t.key = file.getFullPathName();
                        t.file = file;
                        t.shift = state.getProperty (shiftID, 0.0);
                        takes.push_back (std::move (t));
                    }
                }
            }
            else if (auto mc = dynamic_cast<te::MidiClip*> (&c))
            {
                for (int i = 0; i < mc->getNumTakes (true); ++i)
                {
                    if (auto* list = mc->getTakeSequence (i))
                    {
                        Take t;
                        t.state = list->state;
                        t.index = i;
                        t.row = list->state.getProperty (rowID, i);
                        t.key = list->state.getProperty (keyID).toString();
                        t.list = list;
                        t.shift = list->state.getProperty (shiftID, 0.0);
                        takes.push_back (std::move (t));
                    }
                }
            }

            std::stable_sort (takes.begin(), takes.end(), [] (const Take& a, const Take& b) { return a.row < b.row; });
            return takes;
        }

        const Take* findRow (const std::vector<Take>& takes, int row)
        {
            for (auto& t : takes)
                if (t.row == row)
                    return &t;

            return nullptr;
        }

        const Take* findKey (const std::vector<Take>& takes, const juce::String& key)
        {
            for (auto& t : takes)
                if (t.key == key)
                    return &t;

            return nullptr;
        }

        const Take* findActive (te::Clip& c, const std::vector<Take>& takes)
        {
            if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
            {
                auto source = wc->getSourceFileReference().getFile();

                for (auto& t : takes)
                    if (t.file == source)
                        return &t;
            }
            else if (auto mc = dynamic_cast<te::MidiClip*> (&c))
            {
                for (auto& t : takes)
                    if (t.index == mc->getCurrentTake())
                        return &t;
            }

            return nullptr;
        }

        /** Where the start of the clip's content is: in seconds for audio, in beats for MIDI. */
        double getContentStart (te::Clip& c)
        {
            auto start = c.getPosition().getStartOfSource();

            if (dynamic_cast<te::MidiClip*> (&c) != nullptr)
                return c.edit.tempoSequence.toBeats (start).inBeats();

            return start.inSeconds();
        }

        /** The clip's origin: a take's start is this plus its shift. It follows the clip around. */
        double getOrigin (te::Clip& c, const std::vector<Take>& takes)
        {
            auto* active = findActive (c, takes);
            return getContentStart (c) - (active != nullptr ? active->shift : 0.0);
        }

        te::TimePosition getTakeStart (te::Clip& c, double origin, const Take& t)
        {
            if (dynamic_cast<te::MidiClip*> (&c) != nullptr)
                return c.edit.tempoSequence.toTime (te::BeatPosition::fromBeats (origin + t.shift));

            return te::TimePosition::fromSeconds (origin + t.shift);
        }

        te::TimeRange getTakeRange (te::Clip& c, double origin, const Take& t)
        {
            if (t.list != nullptr)
            {
                if (! t.state.hasProperty (startID))
                    return c.getEditTimeRange();

                auto zero = origin + t.shift;
                auto& ts = c.edit.tempoSequence;
                return { ts.toTime (te::BeatPosition::fromBeats (zero + (double) t.state.getProperty (startID))),
                         ts.toTime (te::BeatPosition::fromBeats (zero + (double) t.state.getProperty (endID))) };
            }

            auto seconds = te::AudioFile (c.edit.engine, t.file).getLength();

            if (seconds <= 0.0)
                return c.getEditTimeRange();

            auto start = getTakeStart (c, origin, t);
            return { start, start + te::TimeDuration::fromSeconds (seconds) };
        }

        void switchTo (te::Clip& c, const std::vector<Take>& takes, const Take& t)
        {
            auto offset = c.getPosition().getStart() - getTakeStart (c, getOrigin (c, takes), t);

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
            {
                if (t.file != wc->getSourceFileReference().getFile())
                    wc->getSourceFileReference().setToFile (t.file, te::SourceFileReference::PathStyle::chooseBest, false);
            }
            else if (auto mc = dynamic_cast<te::MidiClip*> (&c))
            {
                mc->setCurrentTake (t.index);
            }

            c.setOffset (juce::jmax (te::TimeDuration(), offset));
        }

        /** Takes only line up on clips that play their content straight. */
        bool canHoldTakes (te::Clip& c)
        {
            if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
                return ! wc->isLooping() && ! wc->getIsReversed() && ! wc->getAutoTempo()
                        && std::abs (wc->getSpeedRatio() - 1.0) < 1.0e-6;

            if (auto mc = dynamic_cast<te::MidiClip*> (&c))
                return ! mc->isLooping();

            return false;
        }

        bool sameKind (te::Clip& a, te::Clip& b)
        {
            return (dynamic_cast<te::MidiClip*> (&a) != nullptr) == (dynamic_cast<te::MidiClip*> (&b) != nullptr);
        }

        /** What identifies the clip's material: take keys, and an audio clip's own file. */
        std::set<juce::String> getKeys (te::Clip& c)
        {
            std::set<juce::String> keys;

            for (auto& t : readTakes (c))
                if (t.key.isNotEmpty())
                    keys.insert (t.key);

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
                keys.insert (wc->getSourceFileReference().getFile().getFullPathName());

            return keys;
        }

        bool sharesTakes (te::Clip& a, te::Clip& b)
        {
            if (! sameKind (a, b))
                return false;

            auto keysA = getKeys (a);

            for (auto& k : getKeys (b))
                if (keysA.count (k) > 0)
                    return true;

            return false;
        }

        /** Writes down what a clip's takes would otherwise leave to defaults, so more can join them:
            lanes, shifts, MIDI keys and the time each MIDI take covers. Plain clips become their own take. */
        void prepareForMoreTakes (te::Clip& c)
        {
            auto* um = &c.edit.getUndoManager();

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
            {
                auto source = wc->getSourceFileReference().getFile();
                auto takes = readTakes (c);

                if (source != juce::File() && findKey (takes, source.getFullPathName()) == nullptr)
                {
                    wc->addTake (source);
                    takes = readTakes (c);
                }

                // Only the first occurrence of each file counts: drop the others.
                auto tree = wc->state.getChildWithName (te::IDs::TAKES);

                for (int i = tree.getNumChildren(); --i >= 0;)
                {
                    auto child = tree.getChild (i);
                    bool used = false;

                    for (auto& t : takes)
                        used = used || t.state == child;

                    if (! used)
                        tree.removeChild (i, um);
                }

                for (auto& t : takes)
                {
                    t.state.setProperty (rowID, t.row, um);
                    t.state.setProperty (shiftID, t.shift, um);
                }

                return;
            }

            auto mc = dynamic_cast<te::MidiClip*> (&c);

            if (mc == nullptr)
                return;

            auto tree = mc->state.getChildWithName (te::IDs::TAKES);

            if (! tree.isValid())
            {
                // Move the one sequence into a take list. The copy goes in first: the clip must
                // never be without a sequence, the editor may be reading it.
                tree = mc->state.getOrCreateChildWithName (te::IDs::TAKES, um);
                auto plain = mc->state.getChildWithName (te::IDs::SEQUENCE);

                if (plain.isValid())
                {
                    tree.addChild (plain.createCopy(), -1, um);
                    mc->state.removeChild (plain, um);
                    mc->setCurrentTake (0);
                }
            }

            auto takes = readTakes (c);
            auto origin = getOrigin (c, takes);
            auto range = c.getEditTimeRange();
            auto& ts = c.edit.tempoSequence;

            for (auto& t : takes)
            {
                auto zero = origin + t.shift;

                if (t.key.isEmpty())
                    t.state.setProperty (keyID, juce::Uuid().toString(), um);

                t.state.setProperty (rowID, t.row, um);
                t.state.setProperty (shiftID, t.shift, um);

                if (! t.state.hasProperty (startID))
                {
                    t.state.setProperty (startID, ts.toBeats (range.getStart()).inBeats() - zero, um);
                    t.state.setProperty (endID, ts.toBeats (range.getEnd()).inBeats() - zero, um);
                }
            }
        }

        void addJoinFades (te::ClipTrack& ct, te::TimeRange range, te::TimeDuration fade)
        {
            // Short fades on both sides of the joins, so switching takes doesn't click.
            for (auto c : ct.getClips())
            {
                if (auto wc = dynamic_cast<te::WaveAudioClip*> (c))
                {
                    auto r = wc->getEditTimeRange();

                    if (std::abs ((r.getEnd() - range.getStart()).inSeconds()) < 1e-6 && wc->getFadeOut() < fade)
                        wc->setFadeOut (fade);
                    if (std::abs ((r.getStart() - range.getEnd()).inSeconds()) < 1e-6 && wc->getFadeIn() < fade)
                        wc->setFadeIn (fade);
                }
            }
        }

        /** A MIDI take edited in one piece differs from its copy in the next. (Audio takes have no children.) */
        bool hasSameContent (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            if (a.getNumChildren() != b.getNumChildren())
                return false;

            for (int i = 0; i < a.getNumChildren(); ++i)
                if (! a.getChild (i).isEquivalentTo (b.getChild (i)))
                    return false;

            return true;
        }

        /** Pieces that play the same take without a break, and hold the same takes, are one clip. */
        bool canRejoin (te::Clip& a, te::Clip& b)
        {
            if (! sameKind (a, b) || a.isMuted() != b.isMuted() || ! canHoldTakes (a) || ! canHoldTakes (b)
                 || std::abs (a.getPosition().getStartOfSource().inSeconds() - b.getPosition().getStartOfSource().inSeconds()) > 1.0e-4)
                return false;

            auto takesA = readTakes (a), takesB = readTakes (b);
            auto* activeA = findActive (a, takesA);
            auto* activeB = findActive (b, takesB);

            if (takesA.size() != takesB.size() || activeA == nullptr || activeB == nullptr || activeA->row != activeB->row)
                return false;

            if (auto wa = dynamic_cast<te::WaveAudioClip*> (&a))
                if (auto wb = dynamic_cast<te::WaveAudioClip*> (&b); wb == nullptr || std::abs (wa->getGainDB() - wb->getGainDB()) > 0.001f)
                    return false;

            auto originA = getOrigin (a, takesA), originB = getOrigin (b, takesB);

            for (size_t i = 0; i < takesA.size(); ++i)
            {
                // Each piece measures its takes from its own origin: compare where they really are.
                if (takesA[i].row != takesB[i].row || takesA[i].key != takesB[i].key
                     || std::abs ((originA + takesA[i].shift) - (originB + takesB[i].shift)) > 1.0e-4
                     || ! hasSameContent (takesA[i].state, takesB[i].state))
                    return false;
            }

            return true;
        }

        void rejoinPieces (te::ClipTrack& ct, te::TimeRange around)
        {
            for (bool again = true; again;)
            {
                again = false;
                auto clips = ct.getClips();

                for (int i = 0; i + 1 < clips.size() && ! again; ++i)
                {
                    te::Clip::Ptr a = clips[i], b = clips[i + 1];
                    auto join = a->getPosition().getEnd();

                    if (std::abs ((join - b->getPosition().getStart()).inSeconds()) < touchingSeconds
                         && join >= around.getStart() && join <= around.getEnd()
                         && canRejoin (*a, *b))
                    {
                        auto end = b->getPosition().getEnd();
                        auto* left = dynamic_cast<te::AudioClipBase*> (a.get());
                        auto* right = dynamic_cast<te::AudioClipBase*> (b.get());
                        auto fadeOut = right != nullptr ? right->getFadeOut() : te::TimeDuration();
                        auto fadeType = right != nullptr ? right->getFadeOutType() : te::AudioFadeCurve::linear;

                        b->removeFromParent();
                        a->setEnd (end, true);

                        if (left != nullptr)
                        {
                            left->setFadeOut (fadeOut);
                            left->setFadeOutType (fadeType);
                        }

                        again = true;
                    }
                }
            }
        }
    }

    //==============================================================================
    std::vector<TakeInfo> getTakes (te::Clip& c)
    {
        std::vector<TakeInfo> result;
        auto takes = readTakes (c);
        auto origin = getOrigin (c, takes);
        auto* active = findActive (c, takes);

        for (auto& t : takes)
        {
            TakeInfo info;
            info.row = t.row;
            info.range = getTakeRange (c, origin, t);
            info.active = &t == active;
            info.file = t.file;
            info.midi = t.list;

            if (t.list != nullptr)
                info.midiStart = te::BeatPosition::fromBeats (origin + t.shift);

            result.push_back (info);
        }

        return result;
    }

    bool hasTakes (te::Clip& c)
    {
        // (Painting asks this for every clip: don't resolve any files for clips without a take list.)
        if (c.state.getChildWithName (te::IDs::TAKES).getNumChildren() < 2)
            return false;

        return getNumTakes (c) > 1;
    }

    int getNumTakes (te::Clip& c)
    {
        return (int) readTakes (c).size();
    }

    int getActiveTake (te::Clip& c)
    {
        auto takes = readTakes (c);
        auto* active = findActive (c, takes);
        return active != nullptr ? active->row : -1;
    }

    void setActiveTake (te::Clip& c, int row)
    {
        auto takes = readTakes (c);

        if (auto* t = findRow (takes, row))
            if (t != findActive (c, takes))
                switchTo (c, takes, *t);
    }

    int getMaxTakesOnTrack (te::Track& t)
    {
        int lanes = 0;

        if (auto ct = dynamic_cast<te::ClipTrack*> (&t))
            for (auto c : ct->getClips())
                if (hasTakes (*c))
                    for (auto& take : readTakes (*c))
                        lanes = juce::jmax (lanes, take.row + 1);

        return lanes;
    }

    bool trackHasTakes (te::Track& t)
    {
        if (auto ct = dynamic_cast<te::ClipTrack*> (&t))
            for (auto c : ct->getClips())
                if (c->state.getChildWithName (te::IDs::TAKES).getNumChildren() > 1)
                    return true;

        return false;
    }

    void compRange (te::Track& track, te::TimeRange range, int row, double joinFadeMs)
    {
        auto ct = dynamic_cast<te::ClipTrack*> (&track);
        if (ct == nullptr || range.isEmpty())
            return;

        const auto fade = te::TimeDuration::fromSeconds (joinFadeMs / 1000.0);
        juce::ReferenceCountedArray<te::Clip> candidates;

        for (auto c : ct->getClips())
            if (c->getEditTimeRange().overlaps (range))
                candidates.add (c);

        for (auto c : candidates)
        {
            auto takes = readTakes (*c);
            auto* take = findRow (takes, row);

            if (take == nullptr || take == findActive (*c, takes))
                continue;

            // As far as the take has material: a shorter pass leaves the rest as it was.
            auto target = range.getIntersectionWith (c->getEditTimeRange())
                               .getIntersectionWith (getTakeRange (*c, getOrigin (*c, takes), *take));

            if (target.getLength().inSeconds() < touchingSeconds)
                continue;

            te::Clip::Ptr piece = c;

            // Split off the part before the range.
            if (piece->getEditTimeRange().getStart() < target.getStart())
                if (auto right = ct->splitClip (*piece, target.getStart()))
                    piece = right;

            // Split off the part after the range.
            if (piece->getEditTimeRange().getEnd() > target.getEnd())
                ct->splitClip (*piece, target.getEnd());

            auto pieceTakes = readTakes (*piece);

            if (auto* t = findRow (pieceTakes, row))
                switchTo (*piece, pieceTakes, *t);

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (piece.get()))
            {
                wc->setFadeIn (fade);
                wc->setFadeOut (fade);
            }

            addJoinFades (*ct, target, fade);
        }

        rejoinPieces (*ct, range);
    }

    void useTake (te::Clip& c, int row)
    {
        if (auto* track = c.getTrack())
            compRange (*track, c.getEditTimeRange(), row);
    }

    void stepTake (te::Clip& c, int direction)
    {
        auto takes = getTakes (c);
        int current = -1;

        for (int i = 0; i < (int) takes.size(); ++i)
            if (takes[(size_t) i].active)
                current = i;

        auto clipRange = c.getEditTimeRange();

        // The next take that has something to play under this clip.
        for (int step = 1; step < (int) takes.size(); ++step)
        {
            auto i = ((current + direction * step) % (int) takes.size() + (int) takes.size()) % (int) takes.size();

            if (takes[(size_t) i].range.overlaps (clipRange))
            {
                useTake (c, takes[(size_t) i].row);
                return;
            }
        }
    }

    void absorbRecording (te::Clip& recorded)
    {
        te::Clip::Ptr keep = &recorded;
        auto* ct = recorded.getClipTrack();

        if (ct == nullptr || ! canHoldTakes (recorded))
            return;

        auto* um = &recorded.edit.getUndoManager();
        auto range = recorded.getEditTimeRange();

        // A loop-recorded audio clip starts on its first pass: play the last complete one, as MIDI does.
        if (dynamic_cast<te::WaveAudioClip*> (&recorded) != nullptr && hasTakes (recorded))
        {
            auto passes = getTakes (recorded);

            for (auto pass = passes.rbegin(); pass != passes.rend(); ++pass)
            {
                if (pass->range.contains (range.reduced (te::TimeDuration::fromSeconds (0.01))))
                {
                    setActiveTake (recorded, pass->row);
                    break;
                }
            }
        }

        juce::ReferenceCountedArray<te::Clip> group, other;

        for (auto c : ct->getClips())
        {
            if (c == &recorded || ! sameKind (*c, recorded)
                 || c->getEditTimeRange().getIntersectionWith (range).getLength().inSeconds() < touchingSeconds)
                continue;

            if (canHoldTakes (*c))
                group.add (c);
            else
                other.add (c);
        }

        // A looped or stretched clip can't line up with takes: the recording replaces that part of it.
        for (auto c : other)
            te::deleteRegionOfClip (*c, range);

        if (group.isEmpty())
        {
            if (recorded.state.getChildWithName (te::IDs::TAKES).getNumChildren() > 1)
                prepareForMoreTakes (recorded);

            return;
        }

        // The whole comp under the recording joins in, not just the pieces it happens to touch.
        for (int i = 0; i < group.size(); ++i)
            for (auto c : getComp (*group[i]))
                group.addIfNotAlreadyThere (c);

        auto covered = range;

        for (auto c : group)
        {
            covered = covered.getUnionWith (c->getEditTimeRange());
            prepareForMoreTakes (*c);
        }

        prepareForMoreTakes (recorded);

        // Every take once, in lanes from the top: what was there first, the recording last.
        struct Merged
        {
            juce::String key;
            int row;
            double start;
            juce::ValueTree state;
        };

        std::vector<Merged> merged;

        auto collect = [&merged] (te::Clip& c)
        {
            auto takes = readTakes (c);
            auto origin = getOrigin (c, takes);

            for (auto& t : takes)
            {
                bool known = false;

                for (auto& m : merged)
                    known = known || m.key == t.key;

                if (! known)
                    merged.push_back ({ t.key, (int) merged.size(), origin + t.shift, t.state.createCopy() });
            }
        };

        std::stable_sort (group.begin(), group.end(), [] (te::Clip* a, te::Clip* b)
                          { return a->getPosition().getStart() < b->getPosition().getStart(); });

        for (auto c : group)
            collect (*c);

        collect (recorded);

        // The recording plays over its range.
        for (auto c : group)
            te::deleteRegionOfClip (*c, range);

        // Whatever is left of the comp (trimming can split a piece), and the recording, get all the takes.
        juce::ReferenceCountedArray<te::Clip> pieces;
        std::set<juce::String> mergedKeys;

        for (auto& m : merged)
            mergedKeys.insert (m.key);

        for (auto c : ct->getClips())
        {
            if (c == &recorded || ! sameKind (*c, recorded) || ! covered.contains (c->getEditTimeRange()))
                continue;

            for (auto& k : getKeys (*c))
                if (mergedKeys.count (k) > 0)
                {
                    pieces.add (c);
                    break;
                }
        }

        pieces.add (&recorded);

        for (auto c : pieces)
        {
            auto takes = readTakes (*c);
            auto origin = getOrigin (*c, takes);
            auto tree = c->state.getChildWithName (te::IDs::TAKES);

            for (auto& m : merged)
            {
                if (auto* existing = findKey (takes, m.key))
                {
                    auto state = existing->state;
                    state.setProperty (rowID, m.row, um);
                }
                else
                {
                    auto copy = m.state.createCopy();
                    copy.setProperty (rowID, m.row, nullptr);
                    copy.setProperty (shiftID, m.start - origin, nullptr);
                    tree.addChild (copy, -1, um);
                }
            }
        }

        addJoinFades (*ct, range, te::TimeDuration::fromSeconds (0.005));

        if (auto wc = dynamic_cast<te::WaveAudioClip*> (&recorded))
        {
            wc->setFadeIn (juce::jmax (wc->getFadeIn(), te::TimeDuration::fromSeconds (0.005)));
            wc->setFadeOut (juce::jmax (wc->getFadeOut(), te::TimeDuration::fromSeconds (0.005)));
        }
    }

    juce::ReferenceCountedArray<te::Clip> getComp (te::Clip& clip)
    {
        juce::ReferenceCountedArray<te::Clip> comp;
        comp.add (&clip);

        auto* ct = clip.getClipTrack();

        if (ct == nullptr)
            return comp;

        auto clips = ct->getClips();
        auto index = clips.indexOf (&clip);

        auto touching = [] (te::Clip& a, te::Clip& b)
        {
            return std::abs ((a.getPosition().getEnd() - b.getPosition().getStart()).inSeconds()) < touchingSeconds;
        };

        for (int i = index; i > 0 && touching (*clips[i - 1], *clips[i]) && sharesTakes (*clips[i - 1], *clips[i]); --i)
            comp.add (clips[i - 1]);

        for (int i = index; i >= 0 && i + 1 < clips.size() && touching (*clips[i], *clips[i + 1]) && sharesTakes (*clips[i], *clips[i + 1]); ++i)
            comp.add (clips[i + 1]);

        return comp;
    }

    juce::Array<te::Clip*> unpackTakesToTracks (te::Clip& clip)
    {
        juce::Array<te::Clip*> created;
        auto& edit = clip.edit;
        auto* after = clip.getTrack();
        auto trackName = after != nullptr ? after->getName() : juce::String ("Take");

        for (auto& take : getTakes (clip))
        {
            auto suffix = " take " + juce::String (take.row + 1);
            auto t = TrackOps::addAudioTrack (edit, nullptr, after, trackName + suffix);

            if (t == nullptr)
                continue;

            if (take.midi != nullptr)
            {
                if (auto mc = te::insertMIDIClip (*t, clip.getName() + suffix, take.range))
                {
                    mc->getSequence().copyFrom (*take.midi, &edit.getUndoManager());
                    mc->setOffset (juce::jmax (te::TimeDuration(), take.range.getStart() - edit.tempoSequence.toTime (take.midiStart)));
                    created.add (mc.get());
                }
            }
            else if (auto nc = t->insertWaveClip (clip.getName() + suffix, take.file, { take.range, {} }, false))
            {
                created.add (nc.get());
            }

            after = t.get();
        }

        return created;
    }

    void removeTakes (te::Clip& clip)
    {
        if (auto mc = dynamic_cast<te::MidiClip*> (&clip))
        {
            if (mc->state.getChildWithName (te::IDs::TAKES).isValid())
                mc->clearTakes();

            return;
        }

        auto takes = clip.state.getChildWithName (te::IDs::TAKES);

        if (takes.isValid())
            clip.state.removeChild (takes, &clip.edit.getUndoManager());
    }

    void deleteUnusedTakes (te::Clip& clip)
    {
        auto* um = &clip.edit.getUndoManager();
        auto comp = getComp (clip);
        std::set<int> used;

        for (auto c : comp)
            used.insert (getActiveTake (*c));

        // The lanes that stay close up.
        std::map<int, int> newRows;

        for (auto row : used)
            if (row >= 0)
                newRows[row] = (int) newRows.size();

        for (auto c : comp)
        {
            prepareForMoreTakes (*c);

            auto takes = readTakes (*c);
            auto tree = c->state.getChildWithName (te::IDs::TAKES);

            if (auto mc = dynamic_cast<te::MidiClip*> (c))
            {
                // The clip's list of sequences only ever grows at its end, and undo must rebuild it
                // in the same order as the tree: from the first take to go, the rest are rewritten.
                int first = tree.getNumChildren();

                for (auto& t : takes)
                    if (used.count (t.row) == 0)
                        first = juce::jmin (first, t.index);

                if (first == tree.getNumChildren())
                    continue;

                auto activeRow = getActiveTake (*c);
                auto oldCount = tree.getNumChildren();
                mc->setCurrentTake (0);

                for (int i = first; i < oldCount; ++i)
                    if (used.count ((int) tree.getChild (i).getProperty (rowID)) > 0)
                        tree.addChild (tree.getChild (i).createCopy(), -1, um);

                for (int i = oldCount; --i >= first;)
                    tree.removeChild (i, um);

                for (auto child : tree)
                    child.setProperty (rowID, newRows[(int) child.getProperty (rowID)], um);

                for (auto& t : readTakes (*c))
                    if (t.row == newRows[activeRow])
                        mc->setCurrentTake (t.index);
            }
            else
            {
                for (auto& t : takes)
                {
                    if (used.count (t.row) == 0)
                        tree.removeChild (t.state, um);
                    else
                        t.state.setProperty (rowID, newRows[t.row], um);
                }
            }
        }
    }
}
