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
        track->state.addListener (this);
        track->edit.getTransport().addChangeListener (this);
        viewState.state.addListener (this);
        buildClips();
    }

    TrackLane::~TrackLane()
    {
        viewState.state.removeListener (this);
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

        // Range selection
        if (viewState.hasRange())
        {
            auto r = viewState.getRange();
            auto x1 = (float) viewState.timeToX (r.getStart());
            auto x2 = (float) viewState.timeToX (r.getEnd());
            g.setColour (Palette::selection);
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, (float) getHeight()));
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

        if (takeLanes != nullptr)
        {
            takeLanes->setBounds (0, base, getWidth(), juce::jmax (0, getHeight() - base));
            takeLanes->repaint();
        }

        repaint();
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
        bool needed = false;
        auto& transport = track->edit.getTransport();

        if (transport.isRecording())
            if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
                for (auto in : track->edit.getEditInputDevices().getDevicesForTargetTrack (*at))
                    if (in->isRecordingActive (track->itemID)
                        && in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                        needed = true;

        if (needed && recordingClip == nullptr)
        {
            recordingClip = std::make_unique<RecordingClipComponent> (track, viewState);
            addAndMakeVisible (*recordingClip);
        }
        else if (! needed)
        {
            recordingClip.reset();
        }
    }

    //==============================================================================
    void TrackLane::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
    {
        if (v == viewState.state)
        {
            if (id == ViewIDs::viewStart || id == ViewIDs::pixelsPerSecond)
                markAndUpdate (needsReposition);
            else if (id == ViewIDs::rangeStart || id == ViewIDs::rangeEnd || id == ViewIDs::gridIndex)
                repaint();

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

    void TrackLane::changeListenerCallback (juce::ChangeBroadcaster*)
    {
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
