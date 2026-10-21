#include "ClipComponent.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/ClipOps.h"
#include "../../Model/CompOps.h"

namespace cdaw
{
    ClipComponent::ClipComponent (ArrangeHost& h, te::Clip::Ptr c)
        : host (h), viewState (h.getViewState()), clip (c)
    {
        clip->state.addListener (this);
        viewState.selectionManager.addChangeListener (this);
        updateThumbnail();
        setRepaintsOnMouseActivity (false);
    }

    ClipComponent::~ClipComponent()
    {
        viewState.selectionManager.removeChangeListener (this);
        clip->state.removeListener (this);
    }

    te::TimeRange ClipComponent::getVisibleTimeRange() const
    {
        return clip->getEditTimeRange();
    }

    void ClipComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
    {
        if (v == clip->state && (id == te::IDs::source || id == te::IDs::currentTake || id == te::IDs::isReversed))
            updateThumbnail();

        repaint();
    }

    void ClipComponent::updateThumbnail()
    {
        auto wac = dynamic_cast<te::WaveAudioClip*> (clip.get());

        if (wac == nullptr)
            return;

        te::AudioFile playback (wac->getPlaybackFile());
        auto file = playback.isValid() ? playback : te::AudioFile (wac->getAudioFile());

        if (! file.isValid())
        {
            thumbnail.reset();
            return;
        }

        if (thumbnail == nullptr)
            thumbnail = std::make_unique<te::SmartThumbnail> (clip->edit.engine, file, *this, &clip->edit);
        else if (file.getFile() != thumbnailFile)
            thumbnail->setNewFile (file);

        thumbnailFile = file.getFile();
        repaint();
    }

    //==============================================================================
    void ClipComponent::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        const bool selected = viewState.selectionManager.isSelected (clip.get());
        const bool muted = clip->isMuted();
        auto colour = clip->getColour();

        if (muted)
            colour = colour.withSaturation (0.0f).withMultipliedBrightness (0.6f);

        g.setColour (colour.withAlpha (0.28f).overlaidWith (Palette::laneBackground.withAlpha (0.4f)));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        auto header = r.removeFromTop (headerHeight);
        g.setColour (colour.withAlpha (muted ? 0.4f : 0.75f));
        g.fillRoundedRectangle (header.toFloat(), 3.0f);
        g.fillRect (header.withTrimmedTop (header.getHeight() / 2));

        g.setColour (colour.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
        g.setFont (juce::FontOptions (11.5f));
        auto label = clip->getName();

        if (auto wc = dynamic_cast<te::WaveAudioClip*> (clip.get()); wc != nullptr && CompOps::hasTakes (*wc))
            label << "   [take " << (CompOps::getActiveTake (*wc) + 1) << "/" << CompOps::getNumTakes (*wc) << "]";
        if (muted)
            label << "   (muted)";

        // Keep the name visible when the clip starts off-screen.
        auto textArea = header.reduced (4, 0);
        if (getX() < 0)
            textArea.setLeft (juce::jmax (textArea.getX(), -getX() + 4));
        g.drawText (label, textArea, juce::Justification::centredLeft, true);

        auto body = r.reduced (0, 1);

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            drawAudio (g, *ac, body);
            drawFades (g, *ac, getLocalBounds());
        }
        else if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        {
            drawMidi (g, *mc, body);
        }

        g.setColour (selected ? Palette::text : colour.darker (0.4f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);
    }

    void ClipComponent::drawAudio (juce::Graphics& g, te::AudioClipBase& ac, juce::Rectangle<int> area)
    {
        if (thumbnail == nullptr)
            return;

        auto colour = ac.isMuted() ? Palette::textDisabled : Palette::text.withAlpha (0.8f);

        if (thumbnail->isGeneratingProxy())
        {
            g.setColour (Palette::textDim);
            g.drawText ("Preparing " + juce::String (juce::roundToInt (thumbnail->getProxyProgress() * 100.0f)) + "%",
                        area, juce::Justification::centred);
            return;
        }

        // Only draw the part that is on screen.
        auto* parent = getParentComponent();
        auto visible = area;
        if (parent != nullptr)
            visible = area.getIntersection (parent->getLocalBounds().translated (-getX(), -getY()));

        if (visible.isEmpty())
            return;

        auto pos = ac.getPosition();
        auto speed = ac.getSpeedRatio();
        auto xToSourceTime = [&] (int localX)
        {
            auto editTime = viewState.xToTime (getX() + localX);
            return te::TimePosition::fromSeconds (((editTime - pos.getStart()) + pos.getOffset()).inSeconds() * speed);
        };

        te::TimeRange sourceRange (xToSourceTime (visible.getX()), xToSourceTime (visible.getRight()));

        if (ac.getIsReversed())
        {
            auto len = ac.getSourceLength();
            sourceRange = { te::TimePosition() + (len - toDuration (sourceRange.getEnd())),
                            te::TimePosition() + (len - toDuration (sourceRange.getStart())) };
        }

        const float gain = ac.getGain();
        g.setColour (colour);

        if (thumbnail->getNumChannels() > 1 && visible.getHeight() > 40)
        {
            auto top = visible.withHeight (visible.getHeight() / 2);
            thumbnail->drawChannel (g, top, sourceRange, 0, gain);
            thumbnail->drawChannel (g, visible.withTrimmedTop (top.getHeight()), sourceRange, 1, gain);
        }
        else
        {
            thumbnail->drawChannels (g, visible, sourceRange, gain);
        }
    }

    void ClipComponent::drawFades (juce::Graphics& g, te::AudioClipBase& ac, juce::Rectangle<int> r)
    {
        auto fadeIn = viewState.durationToPixels (ac.getFadeIn());
        auto fadeOut = viewState.durationToPixels (ac.getFadeOut());
        auto top = (float) r.getY() + headerHeight, bottom = (float) r.getBottom();

        g.setColour (Palette::text.withAlpha (0.55f));

        if (fadeIn > 1.0)
        {
            juce::Path p;
            p.startNewSubPath (0.0f, bottom);
            p.quadraticTo ((float) fadeIn * 0.3f, top, (float) fadeIn, top);
            g.strokePath (p, juce::PathStrokeType (1.2f));
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            p.lineTo (0.0f, top);
            p.closeSubPath();
            g.fillPath (p);
            g.setColour (Palette::text.withAlpha (0.55f));
        }

        if (fadeOut > 1.0)
        {
            auto w = (float) r.getWidth();
            juce::Path p;
            p.startNewSubPath (w - (float) fadeOut, top);
            p.quadraticTo (w - (float) fadeOut * 0.3f, top, w, bottom);
            g.strokePath (p, juce::PathStrokeType (1.2f));
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            p.lineTo (w, top);
            p.closeSubPath();
            g.fillPath (p);
        }

        // Fade handles
        g.setColour (Palette::text.withAlpha (isMouseOver() ? 0.9f : 0.0f));
        g.fillRect (juce::Rectangle<float> ((float) fadeIn - 3.0f, top - 3.0f, 6.0f, 6.0f));
        g.fillRect (juce::Rectangle<float> ((float) r.getWidth() - (float) fadeOut - 3.0f, top - 3.0f, 6.0f, 6.0f));
    }

    void ClipComponent::drawMidi (juce::Graphics& g, te::MidiClip& mc, juce::Rectangle<int> area)
    {
        auto& notes = mc.getSequence().getNotes();

        if (notes.isEmpty())
            return;

        int lo = 127, hi = 0;
        for (auto n : notes)
        {
            lo = juce::jmin (lo, n->getNoteNumber());
            hi = juce::jmax (hi, n->getNoteNumber());
        }

        lo = juce::jmax (0, lo - 2);
        hi = juce::jmin (127, hi + 2);
        auto range = (float) juce::jmax (8, hi - lo + 1);
        auto noteH = juce::jlimit (1.0f, 6.0f, (float) area.getHeight() / range);
        auto clipRange = mc.getEditTimeRange();
        auto clipX = (double) getX();

        for (auto n : notes)
        {
            auto s = mc.getTimeOfContentBeat (n->getStartBeat());
            auto e = mc.getTimeOfContentBeat (n->getEndBeat());

            if (e <= clipRange.getStart() || s >= clipRange.getEnd())
                continue;

            s = juce::jmax (s, clipRange.getStart());
            e = juce::jmin (e, clipRange.getEnd());

            auto x1 = (float) (viewState.timeToX (s) - clipX);
            auto x2 = (float) (viewState.timeToX (e) - clipX);
            auto y = area.getY() + (1.0f - (float) (n->getNoteNumber() - lo + 0.5f) / range) * area.getHeight();

            g.setColour (Palette::text.withAlpha (0.35f + 0.6f * (float) n->getVelocity() / 127.0f));
            g.fillRect (juce::Rectangle<float> (x1, y - noteH * 0.5f, juce::jmax (1.5f, x2 - x1), noteH));
        }
    }

    //==============================================================================
    ClipComponent::Zone ClipComponent::getZone (juce::Point<int> p) const
    {
        constexpr int edge = 6;
        auto w = getWidth();

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            auto fadeIn = (int) viewState.durationToPixels (ac->getFadeIn());
            auto fadeOut = (int) viewState.durationToPixels (ac->getFadeOut());

            if (p.y <= headerHeight + 4 && p.y >= headerHeight - 6)
            {
                if (std::abs (p.x - fadeIn) <= 5 && p.x < w / 2)            return Zone::fadeIn;
                if (std::abs (p.x - (w - fadeOut)) <= 5 && p.x >= w / 2)    return Zone::fadeOut;
            }
        }

        if (w > 3 * edge)
        {
            if (p.x < edge)         return Zone::trimStart;
            if (p.x > w - edge)     return Zone::trimEnd;
        }

        return Zone::body;
    }

    void ClipComponent::updateCursor (Zone z)
    {
        auto tool = viewState.getTool();

        if (tool == Tool::split)                    setMouseCursor (juce::MouseCursor::IBeamCursor);
        else if (tool == Tool::erase)               setMouseCursor (juce::MouseCursor::CrosshairCursor);
        else if (tool == Tool::mute)                setMouseCursor (juce::MouseCursor::PointingHandCursor);
        else if (z == Zone::trimStart || z == Zone::trimEnd) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (z == Zone::fadeIn || z == Zone::fadeOut)     setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        else                                        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void ClipComponent::mouseMove (const juce::MouseEvent& e)
    {
        updateCursor (getZone (e.getPosition()));
        repaint();
    }

    void ClipComponent::mouseDown (const juce::MouseEvent& e)
    {
        auto& sm = viewState.selectionManager;
        auto& edit = clip->edit;
        auto tool = viewState.getTool();
        dragging = false;

        if (e.mods.isPopupMenu())
        {
            if (! sm.isSelected (clip.get()))
                sm.selectOnly (clip.get());

            host.showClipMenu (*clip);
            return;
        }

        if (tool == Tool::split)
        {
            auto t = viewState.snap (viewState.xToTime (getX() + e.x));
            edit.getUndoManager().beginNewTransaction ("Split clip");
            if (auto ct = clip->getClipTrack())
                ct->splitClip (*clip, t);
            return;
        }

        if (tool == Tool::erase)
        {
            edit.getUndoManager().beginNewTransaction ("Delete clip");
            auto keep = clip;
            sm.deselect (clip.get());
            clip->removeFromParent();
            return;
        }

        if (tool == Tool::mute)
        {
            edit.getUndoManager().beginNewTransaction ("Mute clip");
            ClipOps::setMuted ({ clip.get() }, ! clip->isMuted());
            return;
        }

        // Selection: Ctrl toggles, Shift adds, plain click selects only this clip
        // (unless it's already part of a multi-selection about to be dragged).
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            if (sm.isSelected (clip.get()) && e.mods.isCommandDown())
                sm.deselect (clip.get());
            else
                sm.addToSelection (clip.get());
        }
        else if (! sm.isSelected (clip.get()))
        {
            sm.selectOnly (clip.get());
        }

        dragZone = getZone (e.getPosition());
        edit.getUndoManager().beginNewTransaction (dragZone == Zone::body ? "Move clips" : "Edit clip");

        if (dragZone == Zone::body)
            host.beginClipDrag (*clip, e.getEventRelativeTo (&host.asComponent()));
        else
            inhibitor = std::make_unique<te::TransportControl::ReallocationInhibitor> (edit.getTransport());
    }

    void ClipComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() || ! e.mouseWasDraggedSinceMouseDown())
            return;

        auto tool = viewState.getTool();
        if (tool == Tool::split || tool == Tool::erase || tool == Tool::mute)
            return;

        dragging = true;
        auto t = viewState.xToTime (getX() + e.x);

        switch (dragZone)
        {
            case Zone::body:
                host.continueClipDrag (e.getEventRelativeTo (&host.asComponent()));
                break;

            case Zone::trimStart:
                ClipOps::trimStart (*clip, e.mods.isAltDown() ? t : viewState.snap (t));
                break;

            case Zone::trimEnd:
                ClipOps::trimEnd (*clip, e.mods.isAltDown() ? t : viewState.snap (t));
                break;

            case Zone::fadeIn:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                    ac->setFadeIn (juce::jmax (te::TimeDuration(), t - ac->getPosition().getStart()));
                break;

            case Zone::fadeOut:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                    ac->setFadeOut (juce::jmax (te::TimeDuration(), ac->getPosition().getEnd() - t));
                break;
        }
    }

    void ClipComponent::mouseUp (const juce::MouseEvent& e)
    {
        if (dragZone == Zone::body && ! e.mods.isPopupMenu())
        {
            auto tool = viewState.getTool();
            if (tool != Tool::split && tool != Tool::erase && tool != Tool::mute)
                host.endClipDrag (e.getEventRelativeTo (&host.asComponent()));
        }
        else if (dragging && (dragZone == Zone::trimStart || dragZone == Zone::trimEnd))
        {
            if (auto ct = clip->getClipTrack())
                ClipOps::clearRangeForClip (*ct, clip->getEditTimeRange(), { clip.get() });
        }

        inhibitor.reset();
        dragging = false;
    }

    void ClipComponent::mouseDoubleClick (const juce::MouseEvent&)
    {
        host.openClipEditor (*clip);
    }
}
