#include "TrackLane.h"
#include "RecordingClipComponent.h"
#include "TakeLanes.h"
#include "../../Model/CompOps.h"
#include "TimeRuler.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    TrackLane::TrackLane (ArrangeHost& h, te::Track::Ptr t)
        : host (h), viewState (h.getViewState()), track (t)
    {
        setOpaque (true);   // paint() fills the lane
        track->state.addListener (this);
        track->edit.getTransport().addChangeListener (this);
        viewState.selectionManager.addChangeListener (this);
        viewState.state.addListener (this);
        lastSelected = viewState.selectionManager.isSelected (track.get());
        buildClips();
    }

    TrackLane::~TrackLane()
    {
        viewState.state.removeListener (this);
        viewState.selectionManager.removeChangeListener (this);
        track->edit.getTransport().removeChangeListener (this);
        track->state.removeListener (this);
    }

    void TrackLane::paint (juce::Graphics& g)
    {
        const bool selected = viewState.selectionManager.isSelected (track.get());
        g.fillAll (selected ? Palette::laneBackground.brighter (0.04f) : Palette::laneBackground);

        if (track->isFolderTrack())
            g.fillAll (track->getColour().withAlpha (0.06f));

        GridPainter::drawGridLines (g, viewState, getLocalBounds());

        // Range selection (remember where it was drawn, so a change repaints just that strip)
        lastRangeX = {};

        if (viewState.hasRange())
        {
            auto r = viewState.getRange();
            auto x1 = (float) viewState.timeToX (r.getStart());
            auto x2 = (float) viewState.timeToX (r.getEnd());
            g.setColour (Palette::selection);
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, (float) getHeight()));
            lastRangeX = { (int) std::floor (x1), (int) std::ceil (x2) + 1 };
        }

        g.setColour (Palette::windowBackground);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void TrackLane::resized()
    {
        updateClipPositions();
    }

    void TrackLane::updateClipPositions()
    {
        const int base = juce::jmin (getHeight(), EditViewState::getTrackHeight (*track));

        for (auto cc : clips)
        {
            auto range = cc->getClip().getEditTimeRange();
            auto x1 = (int) std::floor (viewState.timeToX (range.getStart()));
            auto x2 = (int) std::ceil (viewState.timeToX (range.getEnd()));
            cc->setBounds (x1, 1, juce::jmax (2, x2 - x1), base - 2);
        }

        // (setBounds repaints where clips were and are; no need to redraw the whole lane.)
        if (takeLanes != nullptr)
            takeLanes->setBounds (0, base, getWidth(), juce::jmax (0, getHeight() - base));
    }

    void TrackLane::repaintRangeChange()
    {
        juce::Range<int> now;

        if (viewState.hasRange())
        {
            auto r = viewState.getRange();
            now = { (int) std::floor (viewState.timeToX (r.getStart())), (int) std::ceil (viewState.timeToX (r.getEnd())) + 1 };
        }

        // Only the strip that changed: where it was drawn and where it goes now.
        auto changed = lastRangeX.isEmpty() ? now : now.isEmpty() ? lastRangeX : lastRangeX.getUnionWith (now);

        if (! changed.isEmpty())
            repaint (changed.getStart() - 1, 0, changed.getLength() + 2, getHeight());
    }

    void TrackLane::updateTakeLanes()
    {
        auto numTakes = CompOps::getMaxTakesOnTrack (*track);

        // Fresh loop recording: show the lanes so the takes can be comped straight away.
        if (numTakes > 1 && numTakes != lastNumTakes && lastNumTakes <= 1 && ! EditViewState::isShowingTakes (*track))
            if (track->edit.getTransport().isRecording() || track->edit.getTransport().isRecordingStopping() || lastNumTakes == 1)
                EditViewState::setShowingTakes (*track, true);

        lastNumTakes = numTakes;
        const bool show = numTakes > 1 && EditViewState::isShowingTakes (*track);

        if (show && takeLanes == nullptr)
        {
            takeLanes = std::make_unique<TakeLanes> (host, track);
            addAndMakeVisible (*takeLanes);
        }
        else if (! show)
        {
            takeLanes.reset();
        }

        host.trackHeightChanged();
        updateClipPositions();
    }

    ClipComponent* TrackLane::findClipComponent (const te::Clip& c) const
    {
        for (auto cc : clips)
            if (&cc->getClip() == &c)
                return cc;

        return nullptr;
    }

    void TrackLane::buildClips()
    {
        clips.clear();

        if (auto ct = dynamic_cast<te::ClipTrack*> (track.get()))
        {
            for (auto c : ct->getClips())
            {
                auto cc = clips.add (new ClipComponent (host, c));
                addAndMakeVisible (cc);
            }
        }

        lastNumTakes = juce::jmax (lastNumTakes, 0);
        updateTakeLanes();
        updateRecordingClip();
    }

    void TrackLane::updateRecordingClip()
    {
        bool needed = false, armed = false;
        auto& transport = track->edit.getTransport();

        if (transport.isRecording())
            if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
                for (auto in : track->edit.getEditInputDevices().getDevicesForTargetTrack (*at))
                    if (in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        armed = armed || in->isRecordingEnabled (track->itemID);
                        needed = needed || in->isRecordingActive (track->itemID);
                    }

        if (needed && recordingClip == nullptr)
        {
            recordingClip = std::make_unique<RecordingClipComponent> (track, viewState);
            addAndMakeVisible (*recordingClip);
        }
        else if (! needed)
        {
            recordingClip.reset();
        }

        // The transport says "recording" before the input actually starts (count-in, the device
        // starting up), and nothing announces that moment: keep checking until the clip appears.
        if (transport.isRecording() && armed && ! needed)
            recordingPoll.startTimer (100);
        else
            recordingPoll.stopTimer();
    }

    //==============================================================================
    void TrackLane::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
    {
        if (v == viewState.state)
        {
            if (id == ViewIDs::viewStart || id == ViewIDs::pixelsPerSecond)
            {
                markAndUpdate (needsReposition);
                repaint();          // the grid scrolls with the view
            }
            else if (id == ViewIDs::rangeStart || id == ViewIDs::rangeEnd)
            {
                repaintRangeChange();
            }
            else if (id == ViewIDs::gridIndex)
            {
                repaint();
            }

            return;
        }

        if (te::Clip::isClipState (v) && (id == te::IDs::start || id == te::IDs::length || id == te::IDs::offset))
            markAndUpdate (needsReposition);
        else if (te::Clip::isClipState (v) && id == te::IDs::source && takeLanes != nullptr)
            takeLanes->repaint();
        else if (v == track->state && id == te::IDs::colour)
            repaint();
        else if (v == track->state && id == ViewIDs::cdawShowTakes)
            updateTakeLanes();
    }

    void TrackLane::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
    {
        if (parent == track->state && te::Clip::isClipState (child))
            markAndUpdate (needsRebuild);
        else if (child.hasType (te::IDs::TAKES) || parent.hasType (te::IDs::TAKES))
            markAndUpdate (needsRebuild);
    }

    void TrackLane::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
    {
        if (parent == track->state && te::Clip::isClipState (child))
            markAndUpdate (needsRebuild);
        else if (child.hasType (te::IDs::TAKES) || parent.hasType (te::IDs::TAKES))
            markAndUpdate (needsRebuild);
    }

    void TrackLane::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
    {
        if (parent == track->state)
            markAndUpdate (needsReposition);
    }

    void TrackLane::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (source == &viewState.selectionManager)
        {
            // The lane background shows whether its track is selected: repaint only when that flips.
            auto selected = viewState.selectionManager.isSelected (track.get());

            if (selected != lastSelected)
            {
                lastSelected = selected;
                repaint();
            }

            return;
        }

        markAndUpdate (needsRecordingUpdate);
    }

    void TrackLane::handleAsyncUpdate()
    {
        if (compareAndReset (needsRebuild))
            buildClips();
        if (compareAndReset (needsReposition))
            updateClipPositions();
        if (compareAndReset (needsRecordingUpdate))
            updateRecordingClip();
    }

    //==============================================================================
    void TrackLane::mouseDown (const juce::MouseEvent& e)
    {
        host.laneMouseDown (*track, e.getEventRelativeTo (&host.asComponent()));
    }

    void TrackLane::mouseDrag (const juce::MouseEvent& e)
    {
        host.laneMouseDrag (*track, e.getEventRelativeTo (&host.asComponent()));
    }

    void TrackLane::mouseUp (const juce::MouseEvent& e)
    {
        host.laneMouseUp (*track, e.getEventRelativeTo (&host.asComponent()));
    }
}
