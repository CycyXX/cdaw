#include "RecordingClipComponent.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    RecordingClipComponent::RecordingClipComponent (te::Track::Ptr t, EditViewState& vs)
        : track (t), viewState (vs)
    {
        setInterceptsMouseClicks (false, false);
        fetchThumbnail();
        startTimerHz (15);
    }

    void RecordingClipComponent::fetchThumbnail()
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
        {
            for (auto idi : at->edit.getEditInputDevices().getDevicesForTargetTrack (*at))
            {
                if (! idi->isRecording (at->itemID))
                    continue;

                punchInTime = idi->getPunchInTime (at->itemID);
                auto file = idi->getRecordingFile (at->itemID);

                if (file != juce::File())
                    thumbnail = at->edit.engine.getRecordingThumbnailManager().getThumbnailFor (file);
            }
        }
    }

    void RecordingClipComponent::timerCallback()
    {
        if (thumbnail == nullptr)
            fetchThumbnail();

        updatePosition();
        repaint();
    }

    void RecordingClipComponent::updatePosition()
    {
        auto& edit = track->edit;
        auto* parent = getParentComponent();
        auto epc = edit.getTransport().getCurrentPlaybackContext();

        if (parent == nullptr || epc == nullptr)
        {
            setBounds ({});
            return;
        }

        auto t1 = punchInTime >= te::TimePosition() ? punchInTime : edit.getTransport().getTimeWhenStarted();
        auto t2 = juce::jmax (t1, epc->getUnloopedPosition());

        if (epc->isLooping())
        {
            auto loop = epc->getLoopTimes();
            if (t2 >= loop.getEnd())
            {
                t1 = juce::jmin (t1, loop.getStart());
                t2 = loop.getEnd();
            }
        }
        else if (edit.recordingPunchInOut)
        {
            auto loop = edit.getTransport().getLoopRange();
            t1 = juce::jlimit (loop.getStart(), loop.getEnd(), t1);
            t2 = juce::jlimit (loop.getStart(), loop.getEnd(), t2);
        }

        shownRange = { t1, t2 };
        auto x1 = (int) viewState.timeToX (t1);
        auto x2 = (int) viewState.timeToX (t2);
        setBounds (x1, 0, juce::jmax (1, x2 - x1), parent->getHeight());
    }

    void RecordingClipComponent::paint (juce::Graphics& g)
    {
        g.setColour (Palette::record.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setColour (Palette::record);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);

        if (thumbnail != nullptr && getWidth() > 2)
        {
            auto startInFile = (shownRange.getStart() - thumbnail->punchInTime).inSeconds();
            auto endInFile = startInFile + shownRange.getLength().inSeconds();
            g.setColour (Palette::text.withAlpha (0.8f));
            thumbnail->thumb->drawChannels (g, getLocalBounds().reduced (0, 4), juce::jmax (0.0, startInFile), endInFile, 1.0f);
        }
    }
}
