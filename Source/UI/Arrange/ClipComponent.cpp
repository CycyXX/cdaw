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
        lastSelected = viewState.selectionManager.isSelected (clip.get());
        updateThumbnail();
        setRepaintsOnMouseActivity (false);
    }

    void ClipComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto selected = viewState.selectionManager.isSelected (clip.get());

        if (selected != lastSelected)
        {
            lastSelected = selected;
            repaint();
        }
    }

    void ClipComponent::repaintHandles()
    {
        // Fade handles, the gain handle and its dB label all live in the top strip.
        repaint (0, 0, getWidth(), headerHeight + 16);
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
        if (clip->isLooping())
            label << "   (loop)";
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
            drawGainHandle (g, *ac);
        }
        else if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        {
            drawMidi (g, *mc, body);
            drawLoopMarks (g, *mc, body);
        }

        g.setColour (selected ? Palette::text : colour.darker (0.4f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);

        drawDragInfo (g);
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

        // Only the columns that are on screen and being repainted (often a thin playhead strip);
        // the vertical layout always uses the whole clip body.
        auto* parent = getParentComponent();
        auto columns = area.getIntersection (g.getClipBounds());
        if (parent != nullptr)
            columns = columns.getIntersection (parent->getLocalBounds().translated (-getX(), -getY()));

        if (columns.isEmpty())
            return;

        auto pos = ac.getPosition();
        const auto speed = ac.getSpeedRatio();
        const auto sourceLength = ac.getSourceLength().inSeconds();
        const bool reversed = ac.getIsReversed();

        // Source time at the left edge of a local x position.
        auto xToSourceSeconds = [&] (int localX)
        {
            auto editTime = viewState.xToTime (getX() + localX);
            auto t = ((editTime - pos.getStart()) + pos.getOffset()).inSeconds() * speed;
            return reversed ? sourceLength - t : t;
        };

        const float gain = ac.getGain();
        g.setColour (colour);

        if (thumbnail->getNumChannels() > 1 && area.getHeight() > 40)
        {
            auto top = area.withHeight (area.getHeight() / 2);
            drawWaveform (g, top, columns, xToSourceSeconds, 0, gain);
            drawWaveform (g, area.withTrimmedTop (top.getHeight()), columns, xToSourceSeconds, 1, gain);
        }
        else
        {
            drawWaveform (g, area, columns, xToSourceSeconds, -1, gain);
        }
    }

    void ClipComponent::drawWaveform (juce::Graphics& g, juce::Rectangle<int> lane, juce::Rectangle<int> columns,
                                      const std::function<double (int)>& xToSourceSeconds, int channel, float gain)
    {
        // One filled path of per-pixel min/max values. (AudioThumbnail::drawChannel draws a
        // rectangle list, which Direct2D turns into a costly sprite batch on every repaint.)
        const int x0 = juce::jmax (columns.getX(), lane.getX());
        const int x1 = juce::jmin (columns.getRight(), lane.getRight());

        if (x1 <= x0 || lane.getHeight() < 2)
            return;

        const int numChannels = thumbnail->getNumChannels();
        const float mid = (float) lane.getCentreY();
        const float half = (float) lane.getHeight() * 0.5f - 0.5f;

        std::vector<float> tops ((size_t) (x1 - x0)), bottoms ((size_t) (x1 - x0));

        for (int x = x0; x < x1; ++x)
        {
            auto a = xToSourceSeconds (x), b = xToSourceSeconds (x + 1);
            if (a > b) std::swap (a, b);

            float lo = 0.0f, hi = 0.0f;

            for (int ch = (channel < 0 ? 0 : channel); ch < (channel < 0 ? numChannels : channel + 1); ++ch)
            {
                float mn = 0.0f, mx = 0.0f;
                thumbnail->getApproximateMinMax (a, b, ch, mn, mx);
                lo = juce::jmin (lo, mn);
                hi = juce::jmax (hi, mx);
            }

            auto top = mid - juce::jlimit (-1.0f, 1.0f, hi * gain) * half;
            auto bottom = mid - juce::jlimit (-1.0f, 1.0f, lo * gain) * half;

            // Keep at least a hairline so quiet passages stay visible.
            if (bottom - top < 1.0f)
            {
                auto c = (top + bottom) * 0.5f;
                top = c - 0.5f;
                bottom = c + 0.5f;
            }

            tops[(size_t) (x - x0)] = top;
            bottoms[(size_t) (x - x0)] = bottom;
        }

        juce::Path p;
        p.startNewSubPath ((float) x0, tops[0]);

        for (int x = x0; x < x1; ++x)
            p.lineTo ((float) x + 0.5f, tops[(size_t) (x - x0)]);

        p.lineTo ((float) x1, tops.back());
        p.lineTo ((float) x1, bottoms.back());

        for (int x = x1 - 1; x >= x0; --x)
            p.lineTo ((float) x + 0.5f, bottoms[(size_t) (x - x0)]);

        p.lineTo ((float) x0, bottoms[0]);
        p.closeSubPath();
        g.fillPath (p);
    }

    void ClipComponent::drawFades (juce::Graphics& g, te::AudioClipBase& ac, juce::Rectangle<int> r)
    {
        auto fadeIn = (float) viewState.durationToPixels (ac.getFadeIn());
        auto fadeOut = (float) viewState.durationToPixels (ac.getFadeOut());
        auto top = (float) r.getY() + headerHeight, bottom = (float) r.getBottom();
        auto w = (float) r.getWidth();

        // Draw the real curve shape: gain 0 at the bottom, 1 at the top.
        auto drawFade = [&] (float x0, float length, bool isFadeIn, te::AudioFadeCurve::Type type)
        {
            juce::Path curve;
            const int steps = juce::jlimit (4, 48, (int) length / 3);

            for (int i = 0; i <= steps; ++i)
            {
                auto alpha = (float) i / (float) steps;
                auto gain = te::AudioFadeCurve::alphaToGainForType (type, isFadeIn ? alpha : 1.0f - alpha);
                juce::Point<float> p (x0 + alpha * length, bottom - gain * (bottom - top));

                if (i == 0) curve.startNewSubPath (p);
                else        curve.lineTo (p);
            }

            // Shade the part the fade removes.
            juce::Path shade (curve);
            shade.lineTo (x0 + length, top);
            shade.lineTo (x0, top);
            shade.closeSubPath();
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillPath (shade);

            g.setColour (Palette::text.withAlpha (0.7f));
            g.strokePath (curve, juce::PathStrokeType (1.3f));
        };

        if (fadeIn > 1.0f)
            drawFade (0.0f, fadeIn, true, ac.getFadeInType());

        if (fadeOut > 1.0f)
            drawFade (w - fadeOut, fadeOut, false, ac.getFadeOutType());

        // Fade handles: always faintly visible, bright when the mouse is over them.
        auto handle = [&] (float x, Zone z)
        {
            auto active = isMouseOver() && (hoverZone == z || (dragging && dragZone == z));
            g.setColour (Palette::text.withAlpha (active ? 1.0f : (isMouseOver() ? 0.55f : 0.0f)));
            g.fillRoundedRectangle (juce::Rectangle<float> (x - 4.0f, top - 4.0f, 8.0f, 8.0f), 2.0f);
        };

        handle (fadeIn, Zone::fadeIn);
        handle (w - fadeOut, Zone::fadeOut);
    }

    juce::Rectangle<int> ClipComponent::getGainHandleBounds() const
    {
        return { getWidth() / 2 - 7, headerHeight + 1, 14, 7 };
    }

    void ClipComponent::drawGainHandle (juce::Graphics& g, te::AudioClipBase& ac)
    {
        if (getWidth() < 60)
            return;

        const auto db = ac.getGainDB();
        const bool changed = std::abs (db) > 0.01f;
        const bool active = hoverZone == Zone::gain || (dragging && dragZone == Zone::gain);

        if (! changed && ! isMouseOver())
            return;

        auto b = getGainHandleBounds().toFloat();
        g.setColour (Palette::text.withAlpha (active ? 1.0f : 0.6f));
        g.fillRoundedRectangle (b, 2.0f);

        // Show the gain next to the handle when it isn't 0 dB (and while hovering it).
        if (changed || active)
        {
            auto text = (db > 0 ? "+" : "") + juce::String (db, 1) + " dB";
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            auto area = juce::Rectangle<float> (b.getRight() + 4.0f, b.getY() - 1.0f, 60.0f, 11.0f);
            g.drawText (text, area.translated (1.0f, 1.0f), juce::Justification::centredLeft, false);
            g.setColour (Palette::text);
            g.drawText (text, area, juce::Justification::centredLeft, false);
        }
    }

    void ClipComponent::drawMidi (juce::Graphics& g, te::MidiClip& mc, juce::Rectangle<int> area)
    {
        // A looping clip plays its loop range over and over; draw what plays.
        auto& sequence = mc.isLooping() ? mc.getSequenceLooped() : mc.getSequence();
        auto& notes = sequence.getNotes();

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

        // Notes outside the area being repainted are skipped before any tempo maths.
        auto dirty = g.getClipBounds();
        auto firstBeat = mc.getContentBeatAtTime (viewState.xToTime (clipX + dirty.getX() - 2));
        auto lastBeat = mc.getContentBeatAtTime (viewState.xToTime (clipX + dirty.getRight() + 2));

        for (auto n : notes)
        {
            if (n->getEndBeat() < firstBeat || n->getStartBeat() > lastBeat)
                continue;

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

    void ClipComponent::drawLoopMarks (juce::Graphics& g, te::MidiClip& mc, juce::Rectangle<int> area)
    {
        if (! mc.isLooping())
            return;

        // A dashed line where each repetition starts.
        auto& ts = mc.edit.tempoSequence;
        auto loopLen = mc.getLoopLengthBeats();

        if (loopLen <= te::BeatDuration::fromBeats (0.01))
            return;

        auto startBeat = ts.toBeats (mc.getPosition().getStart());
        auto endBeat = ts.toBeats (mc.getPosition().getEnd());
        auto clipX = (double) getX();
        const float dashes[] = { 3.0f, 3.0f };

        for (auto b = startBeat + loopLen; b < endBeat; b = b + loopLen)
        {
            auto x = (float) (viewState.timeToX (ts.toTime (b)) - clipX);
            g.setColour (Palette::text.withAlpha (0.45f));
            g.drawDashedLine (juce::Line<float> (x, (float) area.getY(), x, (float) area.getBottom()), dashes, 2, 1.0f);
        }
    }

    void ClipComponent::drawDragInfo (juce::Graphics& g)
    {
        if (dragInfo.isEmpty())
            return;

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        auto w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), dragInfo) + 10;
        auto box = juce::Rectangle<int> (juce::jmax (2, juce::jmin (getWidth() - w - 2, getWidth() / 2 - w / 2)),
                                         headerHeight + 10, w, 16);
        g.setColour (Palette::panelRaised.withAlpha (0.92f));
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        g.setColour (Palette::text);
        g.drawText (dragInfo, box, juce::Justification::centred, false);
    }

    //==============================================================================
    bool ClipComponent::isOverFade (juce::Point<int> p, bool fadeIn) const
    {
        auto ac = dynamic_cast<te::AudioClipBase*> (clip.get());
        if (ac == nullptr || p.y < headerHeight)
            return false;

        auto len = (int) viewState.durationToPixels (fadeIn ? ac->getFadeIn() : ac->getFadeOut());
        return len > 2 && (fadeIn ? p.x < len : p.x > getWidth() - len);
    }

    ClipComponent::Zone ClipComponent::getZone (juce::Point<int> p, juce::ModifierKeys mods) const
    {
        constexpr int edge = 6;
        auto w = getWidth();

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            auto fadeIn = (int) viewState.durationToPixels (ac->getFadeIn());
            auto fadeOut = (int) viewState.durationToPixels (ac->getFadeOut());

            if (p.y <= headerHeight + 5 && p.y >= headerHeight - 6)
            {
                if (std::abs (p.x - fadeIn) <= 5 && p.x < w / 2)            return Zone::fadeIn;
                if (std::abs (p.x - (w - fadeOut)) <= 5 && p.x >= w / 2)    return Zone::fadeOut;
            }

            if (w >= 60 && getGainHandleBounds().expanded (4, 3).contains (p))
                return Zone::gain;
        }

        if (w > 3 * edge)
        {
            if (p.x < edge)         return Zone::trimStart;
            if (p.x > w - edge)     return Zone::trimEnd;
        }

        if (mods.isCommandDown() && mods.isAltDown())
            return Zone::slip;

        return Zone::body;
    }

    void ClipComponent::updateCursor (Zone z)
    {
        auto tool = viewState.getTool();

        if (tool == Tool::split)                    setMouseCursor (juce::MouseCursor::IBeamCursor);
        else if (tool == Tool::erase)               setMouseCursor (juce::MouseCursor::CrosshairCursor);
        else if (tool == Tool::mute)                setMouseCursor (juce::MouseCursor::PointingHandCursor);
        else if (z == Zone::trimStart || z == Zone::trimEnd) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (z == Zone::fadeIn || z == Zone::fadeOut)     setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (z == Zone::gain)                   setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else if (z == Zone::slip)                   setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else                                        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void ClipComponent::mouseMove (const juce::MouseEvent& e)
    {
        auto z = getZone (e.getPosition(), e.mods);

        // Handles highlight under the mouse: repaint their strip only when that changes.
        if (z != hoverZone)
        {
            hoverZone = z;
            repaintHandles();
        }

        updateCursor (z);
    }

    void ClipComponent::mouseEnter (const juce::MouseEvent&)
    {
        // Fade and gain handles appear while the mouse is over the clip.
        if (dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr)
            repaintHandles();
    }

    void ClipComponent::mouseExit (const juce::MouseEvent&)
    {
        hoverZone = Zone::body;

        if (dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr)
            repaintHandles();
    }

    void ClipComponent::showFadeMenu (bool fadeIn)
    {
        auto ac = dynamic_cast<te::AudioClipBase*> (clip.get());
        if (ac == nullptr)
            return;

        te::Clip::Ptr keep = clip;
        auto current = fadeIn ? ac->getFadeInType() : ac->getFadeOutType();
        auto* um = &clip->edit.getUndoManager();
        juce::PopupMenu m;
        m.addSectionHeader (fadeIn ? "Fade In Shape" : "Fade Out Shape");

        for (auto [type, name] : { std::pair { te::AudioFadeCurve::linear,  "Linear" },
                                   std::pair { te::AudioFadeCurve::convex,  "Convex (equal power)" },
                                   std::pair { te::AudioFadeCurve::concave, "Concave" },
                                   std::pair { te::AudioFadeCurve::sCurve,  "S-Curve" } })
        {
            m.addItem (name, true, current == type, [keep, fadeIn, type = type, um]
            {
                if (auto a = dynamic_cast<te::AudioClipBase*> (keep.get()))
                {
                    um->beginNewTransaction ("Fade shape");
                    if (fadeIn) a->setFadeInType (type);
                    else        a->setFadeOutType (type);
                }
            });
        }

        m.addSeparator();
        m.addItem (fadeIn ? "Remove Fade In" : "Remove Fade Out", [keep, fadeIn, um]
        {
            if (auto a = dynamic_cast<te::AudioClipBase*> (keep.get()))
            {
                um->beginNewTransaction ("Remove fade");
                if (fadeIn) a->setFadeIn ({});
                else        a->setFadeOut ({});
            }
        });

        m.showMenuAsync ({});
    }

    void ClipComponent::mouseDown (const juce::MouseEvent& e)
    {
        auto& sm = viewState.selectionManager;
        auto& edit = clip->edit;
        auto tool = viewState.getTool();
        dragging = false;
        dragInfo.clear();

        if (e.mods.isPopupMenu())
        {
            // Right-click on a fade: its shape.
            for (bool fadeIn : { true, false })
                if (isOverFade (e.getPosition(), fadeIn)
                     || getZone (e.getPosition(), {}) == (fadeIn ? Zone::fadeIn : Zone::fadeOut))
                {
                    showFadeMenu (fadeIn);
                    return;
                }

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

        dragZone = getZone (e.getPosition(), e.mods);

        // Selection: Ctrl toggles, Shift adds, plain click selects only this clip
        // (unless it's already part of a multi-selection about to be dragged).
        if (dragZone == Zone::slip)
        {
            if (! sm.isSelected (clip.get()))
                sm.selectOnly (clip.get());
        }
        else if (e.mods.isShiftDown() || e.mods.isCommandDown())
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

        const char* undoName = "Edit clip";
        switch (dragZone)
        {
            case Zone::body:        undoName = "Move clips"; break;
            case Zone::gain:        undoName = "Clip gain"; break;
            case Zone::slip:        undoName = "Slip clip content"; break;
            case Zone::fadeIn:
            case Zone::fadeOut:     undoName = "Fade"; break;
            case Zone::trimStart:
            case Zone::trimEnd:     undoName = "Trim clip"; break;
        }

        edit.getUndoManager().beginNewTransaction (undoName);

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
            dragStartGainDb = ac->getGainDB();

        dragStartOffset = clip->getPosition().getOffset();

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
        auto ms = [] (te::TimeDuration d) { return juce::String (juce::roundToInt (d.inSeconds() * 1000.0)) + " ms"; };

        switch (dragZone)
        {
            case Zone::body:
                host.continueClipDrag (e.getEventRelativeTo (&host.asComponent()));
                break;

            case Zone::trimStart:
                ClipOps::trimStart (*clip, e.mods.isAltDown() ? t : viewState.snap (t));
                dragInfo = "Length " + juce::String (clip->getPosition().getLength().inSeconds(), 2) + " s";
                break;

            case Zone::trimEnd:
                ClipOps::trimEnd (*clip, e.mods.isAltDown() ? t : viewState.snap (t));
                dragInfo = "Length " + juce::String (clip->getPosition().getLength().inSeconds(), 2) + " s";
                break;

            case Zone::fadeIn:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                {
                    ac->setFadeIn (juce::jlimit (te::TimeDuration(), ac->getPosition().getLength(), t - ac->getPosition().getStart()));
                    dragInfo = "Fade in " + ms (ac->getFadeIn());
                }
                break;

            case Zone::fadeOut:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                {
                    ac->setFadeOut (juce::jlimit (te::TimeDuration(), ac->getPosition().getLength(), ac->getPosition().getEnd() - t));
                    dragInfo = "Fade out " + ms (ac->getFadeOut());
                }
                break;

            case Zone::gain:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                {
                    // 4 px per dB (Shift for fine steps), -48 .. +12 dB.
                    auto perPixel = e.mods.isShiftDown() ? 0.05f : 0.25f;
                    auto db = juce::jlimit (-48.0f, 12.0f, dragStartGainDb - (float) e.getDistanceFromDragStartY() * perPixel);
                    ac->setGainDB (std::round (db * 10.0f) / 10.0f);
                    dragInfo.clear();   // the handle shows the value
                }
                break;

            case Zone::slip:
            {
                // The content follows the mouse; the clip stays where it is.
                auto delta = te::TimeDuration::fromSeconds (e.getDistanceFromDragStartX() / viewState.pixelsPerSecond.get());
                auto offset = dragStartOffset - delta;
                auto maxOffset = te::TimeDuration::fromSeconds (1.0e7);

                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()); ac != nullptr && ! ac->isLooping())
                    maxOffset = juce::jmax (te::TimeDuration(),
                                            te::TimeDuration::fromSeconds (ac->getSourceLength().inSeconds() / ac->getSpeedRatio())
                                              - ac->getPosition().getLength());

                offset = juce::jlimit (te::TimeDuration(), maxOffset, offset);
                clip->setOffset (offset);
                auto moved = offset - dragStartOffset;
                dragInfo = "Content " + juce::String (moved > te::TimeDuration() ? "-" : "+")
                           + ms (te::TimeDuration::fromSeconds (std::abs (moved.inSeconds())));
                break;
            }
        }

        repaint();
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
        dragInfo.clear();
        repaint();
    }

    void ClipComponent::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // Double-click the gain handle: back to 0 dB.
        if (getZone (e.getPosition(), e.mods) == Zone::gain)
        {
            if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
            {
                clip->edit.getUndoManager().beginNewTransaction ("Reset clip gain");
                ac->setGainDB (0.0f);
            }
            return;
        }

        host.openClipEditor (*clip);
    }
}
