#include "ArrangeView.h"
#include "../LookAndFeel/Palette.h"
#include "../../App/AppContext.h"
#include "../../Commands/CommandIDs.h"
#include "../../Model/ClipOps.h"
#include "../../Model/AutomationOps.h"
#include "../../Model/CompOps.h"
#include "../../Model/ProjectSession.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    //==============================================================================
    /** Drawn above the lanes: playhead, cursor, drag ghosts, rubber band, drop marker. */
    struct ArrangeView::Overlay : public juce::Component
    {
        explicit Overlay (ArrangeView& o) : owner (o)
        {
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& r : ghosts)
            {
                g.setColour (Palette::text.withAlpha (0.12f));
                g.fillRoundedRectangle (r.toFloat(), 3.0f);
                g.setColour (Palette::text.withAlpha (0.8f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.5f);
            }

            if (! rubberBand.isEmpty())
            {
                g.setColour (Palette::selection);
                g.fillRect (rubberBand);
                g.setColour (Palette::accent);
                g.drawRect (rubberBand);
            }

            if (dropX >= 0)
            {
                g.setColour (Palette::accent);
                g.fillRect (dropX - 1, 0, 2, getHeight());
                if (dropY >= 0)
                    g.drawRect (juce::Rectangle<int> (dropX, dropY, 120, 4));
            }

            if (playheadX >= 0)
            {
                g.setColour (Palette::playhead);
                g.fillRect ((float) playheadX - 0.5f, 0.0f, 1.5f, (float) getHeight());
            }
        }

        /** Area currently drawn by ghosts, rubber band and drop marker. */
        juce::Rectangle<int> getDrawnArea() const
        {
            juce::Rectangle<int> r;

            for (auto& g : ghosts)
                r = r.getUnion (g.expanded (2));

            if (! rubberBand.isEmpty())
                r = r.getUnion (rubberBand.expanded (2));

            if (dropX >= 0)
                r = r.getUnion ({ dropX - 2, 0, 124, getHeight() });

            return r;
        }

        /** Applies a change to the drawn items and repaints only what moved (old area + new area). */
        template <typename Fn>
        void change (Fn&& fn)
        {
            auto before = getDrawnArea();
            fn();
            auto dirty = before.getUnion (getDrawnArea());

            if (! dirty.isEmpty())
                repaint (dirty);
        }

        ArrangeView& owner;
        juce::Array<juce::Rectangle<int>> ghosts;
        juce::Rectangle<int> rubberBand;
        int dropX = -1, dropY = -1;
        double playheadX = -1.0;
    };

    /** Background below the last track: click deselects, double-click adds a track. */
    struct ArrangeView::EmptyArea : public juce::Component
    {
        explicit EmptyArea (ArrangeView& o) : owner (o)    { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::windowBackground);
            g.setColour (Palette::textDisabled);
            g.setFont (juce::FontOptions (13.0f));

            if (getHeight() > 60)
                g.drawText ("Drop audio files here, or double-click to add a track", getLocalBounds().removeFromTop (60),
                            juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.viewState.selectionManager.deselectAll();
            owner.viewState.clearRange();

            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                auto add = [this] (const char* name, int cmd) { return std::make_pair (juce::String (name), cmd); };
                for (auto [name, cmd] : { add ("Add Audio Track", CommandIDs::addAudioTrack),
                                          add ("Add Instrument Track...", CommandIDs::addInstrumentTrack),
                                          add ("Add FX Track", CommandIDs::addFxTrack),
                                          add ("Add Group Track", CommandIDs::addGroupTrack),
                                          add ("Add Folder Track", CommandIDs::addFolderTrack) })
                    m.addItem (name, [this, cmd = cmd] { if (owner.onTrackCommand) owner.onTrackCommand (cmd); });

                m.showMenuAsync ({});
            }
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (owner.onTrackCommand)
                owner.onTrackCommand (CommandIDs::addAudioTrack);
        }

        ArrangeView& owner;
    };

    //==============================================================================
    ArrangeView::ArrangeView (ProjectSession& s, EditViewState& vs)
        : session (s), edit (s.getEdit()), viewState (vs), ruler (vs)
    {
        emptyArea = std::make_unique<EmptyArea> (*this);
        overlay = std::make_unique<Overlay> (*this);
        arranger = std::make_unique<ArrangerLane> (viewState, headerWidth);
        arranger->onSectionsChanged = [this] { lanesArea.repaint(); };

        addAndMakeVisible (ruler);
        addChildComponent (*arranger);
        arranger->setVisible (isArrangerShown());
        addAndMakeVisible (headersArea);
        addAndMakeVisible (lanesArea);
        lanesArea.addAndMakeVisible (*emptyArea);
        addAndMakeVisible (*overlay);
        addAndMakeVisible (vScroll);
        addAndMakeVisible (hScroll);

        vScroll.addListener (this);
        hScroll.addListener (this);
        vScroll.setAutoHide (false);
        hScroll.setAutoHide (false);

        edit.state.addListener (this);
        viewState.selectionManager.addChangeListener (this);

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onVBlank(); });

        buildTracks();
    }

    ArrangeView::~ArrangeView()
    {
        vblank.reset();
        viewState.selectionManager.removeChangeListener (this);
        edit.state.removeListener (this);
    }

    //==============================================================================
    void ArrangeView::buildTracks()
    {
        autoHeaders.clear();
        autoLanes.clear();
        autoLaneTrackIndex.clear();
        headers.clear();
        lanes.clear();

        for (auto t : TrackOps::getArrangeTracks (edit))
        {
            auto* h = headers.add (new TrackHeader (*this, t));
            headersArea.addAndMakeVisible (h);

            auto* l = lanes.add (new TrackLane (*this, t));
            lanesArea.addAndMakeVisible (l);

            for (auto& key : AutomationOps::getLaneKeys (*t))
            {
                if (auto* p = AutomationOps::findParameter (*t, key))
                {
                    headersArea.addAndMakeVisible (autoHeaders.add (new AutomationLaneHeader (*this, t, p)));
                    lanesArea.addAndMakeVisible (autoLanes.add (new AutomationLane (*this, t, p)));
                    autoLaneTrackIndex.add (headers.size() - 1);
                }
            }
        }

        layoutTracks();
    }

    int ArrangeView::getTotalTracksHeight() const
    {
        int h = 0;
        for (auto header : headers)
            h += EditViewState::getDisplayHeight (header->getTrack());
        return h + autoLanes.size() * AutomationLane::defaultHeight;
    }

    void ArrangeView::layoutTracks()
    {
        auto totalHeight = getTotalTracksHeight();
        auto maxScroll = juce::jmax (0, totalHeight - lanesArea.getHeight() + 80);

        if (viewState.scrollY.get() > maxScroll)
            viewState.scrollY = maxScroll;

        int y = -viewState.scrollY.get();

        for (int i = 0; i < headers.size(); ++i)
        {
            auto h = EditViewState::getDisplayHeight (headers[i]->getTrack());
            headers[i]->setBounds (0, y, headersArea.getWidth(), h);
            lanes[i]->setBounds (0, y, lanesArea.getWidth(), h);
            y += h;

            for (int a = 0; a < autoLanes.size(); ++a)
            {
                if (autoLaneTrackIndex[a] != i)
                    continue;

                const int ah = AutomationLane::defaultHeight;
                autoHeaders[a]->setBounds (0, y, headersArea.getWidth(), ah);
                autoLanes[a]->setBounds (0, y, lanesArea.getWidth(), ah);
                y += ah;
            }
        }

        emptyArea->setBounds (0, juce::jmax (0, y), lanesArea.getWidth(), juce::jmax (0, lanesArea.getHeight() - y));
        updateScrollBars();
        overlay->repaint();
    }

    double ArrangeView::getContentLengthSeconds() const
    {
        auto visible = lanesArea.getWidth() / viewState.pixelsPerSecond.get();
        return juce::jmax (edit.getLength().inSeconds() + 60.0, viewState.viewStart.get() + visible, 300.0);
    }

    void ArrangeView::updateScrollBars()
    {
        auto total = getTotalTracksHeight() + 80;
        vScroll.setRangeLimits (0.0, juce::jmax ((double) total, (double) lanesArea.getHeight()));
        vScroll.setCurrentRange (viewState.scrollY.get(), lanesArea.getHeight(), juce::dontSendNotification);

        auto visibleSecs = lanesArea.getWidth() / viewState.pixelsPerSecond.get();
        hScroll.setRangeLimits (0.0, getContentLengthSeconds());
        hScroll.setCurrentRange (viewState.viewStart.get(), visibleSecs, juce::dontSendNotification);
    }

    bool ArrangeView::isArrangerShown() const
    {
        return (bool) viewState.state.getProperty (ViewIDs::showArranger, true);
    }

    void ArrangeView::setArrangerShown (bool shown)
    {
        viewState.state.setProperty (ViewIDs::showArranger, shown, nullptr);
        arranger->setVisible (shown);
        resized();
    }

    void ArrangeView::resized()
    {
        auto r = getLocalBounds();
        const int arrangerH = arranger->isVisible() ? ArrangerLane::height : 0;
        hScroll.setBounds (r.removeFromBottom (scrollBarSize).withTrimmedLeft (headerWidth).withTrimmedRight (scrollBarSize));
        vScroll.setBounds (r.removeFromRight (scrollBarSize).withTrimmedTop (rulerHeight + arrangerH));

        auto top = r.removeFromTop (rulerHeight);
        ruler.setBounds (top.withTrimmedLeft (headerWidth));

        if (arrangerH > 0)
            arranger->setBounds (r.removeFromTop (arrangerH));

        headersArea.setBounds (r.removeFromLeft (headerWidth));
        lanesArea.setBounds (r);
        overlay->setBounds (r);
        layoutTracks();
    }

    void ArrangeView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::windowBackground);

        // Top-left corner above the headers.
        auto corner = juce::Rectangle<int> (0, 0, headerWidth, rulerHeight);
        g.setColour (Palette::panelBackground);
        g.fillRect (corner);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (headers.size()) + " tracks", corner.reduced (8, 0), juce::Justification::centredLeft);
        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) headerWidth);
    }

    //==============================================================================
    void ArrangeView::onVBlank()
    {
        auto& transport = edit.getTransport();
        auto pos = transport.getPosition();

        if (auto epc = transport.getCurrentPlaybackContext(); epc != nullptr && transport.isPlaying())
            pos = epc->getAudibleTimelineTime();

        auto x = viewState.timeToX (pos);

        // Follow playback by pages.
        if (transport.isPlaying() && viewState.followPlayhead.get() && ! transport.isUserDragging())
        {
            auto w = (double) lanesArea.getWidth();
            if (x > w * 0.92 || x < 0.0)
            {
                viewState.viewStart = juce::jmax (0.0, pos.inSeconds() - (w * 0.05) / viewState.pixelsPerSecond.get());
                x = viewState.timeToX (pos);
            }
        }

        if (std::abs (x - lastPlayheadX) >= 0.5)
        {
            auto old = lastPlayheadX;
            lastPlayheadX = x;
            overlay->playheadX = x;

            auto h = overlay->getHeight();
            if (old >= -2)
                overlay->repaint ((int) old - 2, 0, 5, h);
            overlay->repaint ((int) x - 2, 0, 5, h);
            ruler.repaint ((int) juce::jmin (old, x) - 8, 0, (int) std::abs (x - old) + 16, ruler.getHeight());
        }
    }

    void ArrangeView::scrollToTime (te::TimePosition t)
    {
        auto visible = lanesArea.getWidth() / viewState.pixelsPerSecond.get();
        auto x = viewState.timeToX (t);

        if (x < 0 || x > lanesArea.getWidth())
            viewState.viewStart = juce::jmax (0.0, t.inSeconds() - visible * 0.1);
    }

    //==============================================================================
    void ArrangeView::zoomBy (double factor)
    {
        auto anchor = juce::jlimit (0.0, (double) lanesArea.getWidth(), lastPlayheadX);
        viewState.zoomHorizontally (factor, anchor);
    }

    void ArrangeView::zoomToFit()
    {
        auto len = juce::jmax (8.0, edit.getLength().inSeconds());
        viewState.setVisibleRange (0.0, len * 1.05, juce::jmax (100, lanesArea.getWidth()));
    }

    void ArrangeView::zoomTrackHeights (double factor)
    {
        for (auto h : headers)
            EditViewState::setTrackHeight (h->getTrack(), juce::roundToInt (EditViewState::getTrackHeight (h->getTrack()) * factor));

        layoutTracks();
    }

    void ArrangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        auto local = e.getEventRelativeTo (this);

        if (e.mods.isCommandDown())
        {
            auto anchor = (double) (local.x - lanesArea.getX());
            viewState.zoomHorizontally (wheel.deltaY > 0 ? 1.18 : 1.0 / 1.18, juce::jmax (0.0, anchor));
        }
        else if (e.mods.isAltDown())
        {
            zoomTrackHeights (wheel.deltaY > 0 ? 1.12 : 1.0 / 1.12);
        }
        else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        {
            auto delta = (std::abs (wheel.deltaX) > 0 ? wheel.deltaX : wheel.deltaY);
            auto secs = -delta * 400.0 / viewState.pixelsPerSecond.get();
            viewState.viewStart = juce::jmax (0.0, viewState.viewStart.get() + secs);
        }
        else
        {
            viewState.scrollY = juce::jmax (0, viewState.scrollY.get() - juce::roundToInt (wheel.deltaY * 300.0));
        }
    }

    void ArrangeView::mouseDown (const juce::MouseEvent&)
    {
        viewState.selectionManager.deselectAll();
    }

    void ArrangeView::scrollBarMoved (juce::ScrollBar* bar, double start)
    {
        if (bar == &vScroll)
            viewState.scrollY = juce::roundToInt (start);
        else
            viewState.viewStart = juce::jmax (0.0, start);
    }

    //==============================================================================
    int ArrangeView::getTrackIndexAt (int y) const
    {
        auto laneY = y - lanesArea.getY();

        for (int i = 0; i < lanes.size(); ++i)
            if (laneY >= lanes[i]->getY() && laneY < lanes[i]->getBottom())
                return i;

        return -1;
    }

    te::Track* ArrangeView::getTrackAt (int y) const
    {
        auto i = getTrackIndexAt (y);
        return i >= 0 ? &lanes[i]->getTrack() : nullptr;
    }

    te::Track* ArrangeView::getFirstSelectedTrack() const
    {
        for (auto s : viewState.selectionManager.getSelectedObjects())
            if (auto t = dynamic_cast<te::Track*> (s))
                return t;

        for (auto s : viewState.selectionManager.getSelectedObjects())
            if (auto c = dynamic_cast<te::Clip*> (s))
                return c->getTrack();

        return nullptr;
    }

    //==============================================================================
    void ArrangeView::beginClipDrag (te::Clip& grabbed, const juce::MouseEvent& e)
    {
        dragClips = ClipOps::getSelectedClips (viewState.selectionManager);

        if (! dragClips.contains (&grabbed))
            dragClips = { &grabbed };

        auto tracks = TrackOps::getArrangeTracks (edit);
        dragOriginalRanges.clear();
        dragOriginalTrackIndex.clear();

        for (auto c : dragClips)
        {
            dragOriginalRanges.add (c->getEditTimeRange());
            dragOriginalTrackIndex.add (tracks.indexOf (c->getTrack()));
        }

        dragAnchorStart = grabbed.getPosition().getStart();
        dragStartPos = e.getPosition();
        dragStartTrackIndex = getTrackIndexAt (e.y);
        dragDelta = {};
        dragTrackDelta = 0;
    }

    void ArrangeView::continueClipDrag (const juce::MouseEvent& e)
    {
        if (dragClips.isEmpty())
            return;

        auto dx = e.x - dragStartPos.x;
        auto rawStart = te::TimePosition::fromSeconds (juce::jmax (0.0, dragAnchorStart.inSeconds() + dx / viewState.pixelsPerSecond.get()));
        auto newStart = e.mods.isShiftDown() ? rawStart : viewState.snap (rawStart);
        dragDelta = newStart - dragAnchorStart;

        auto idx = getTrackIndexAt (e.y);
        dragTrackDelta = (idx >= 0 && dragStartTrackIndex >= 0) ? idx - dragStartTrackIndex : dragTrackDelta;
        dragCopy = e.mods.isAltDown();

        overlay->change ([&]
        {
            overlay->ghosts.clear();

            for (int i = 0; i < dragClips.size(); ++i)
            {
                auto range = dragOriginalRanges[i] + dragDelta;
                auto row = juce::jlimit (0, lanes.size() - 1, dragOriginalTrackIndex[i] + dragTrackDelta);

                if (! juce::isPositiveAndBelow (row, lanes.size()))
                    continue;

                auto* lane = lanes[row];
                auto x1 = (int) viewState.timeToX (range.getStart());
                auto x2 = (int) viewState.timeToX (range.getEnd());
                overlay->ghosts.add (juce::Rectangle<int> (x1, lane->getY() + 1, juce::jmax (2, x2 - x1), lane->getHeight() - 2));
            }
        });
    }

    void ArrangeView::endClipDrag (const juce::MouseEvent& e)
    {
        overlay->change ([this] { overlay->ghosts.clear(); });

        if (! dragClips.isEmpty() && e.mouseWasDraggedSinceMouseDown()
             && (dragDelta != te::TimeDuration() || dragTrackDelta != 0))
        {
            auto moved = ClipOps::moveClips (edit, dragClips, dragDelta, dragTrackDelta, dragCopy);

            if (dragCopy)
            {
                viewState.selectionManager.deselectAll();
                for (auto c : moved)
                    viewState.selectionManager.addToSelection (c);
            }
        }

        dragClips.clear();
    }

    //==============================================================================
    void ArrangeView::laneMouseDown (te::Track& track, const juce::MouseEvent& e)
    {
        auto& sm = viewState.selectionManager;
        auto t = viewState.xToTime (e.x - lanesArea.getX());
        laneDragStart = e.getPosition();
        laneDragStartTime = e.mods.isShiftDown() ? t : viewState.snap (t);
        laneDragTrack = &track;
        laneDrag = LaneDrag::none;

        if (e.mods.isPopupMenu())
        {
            sm.selectOnly (&track);
            showTrackMenu (track);
            return;
        }

        switch (viewState.getTool())
        {
            case Tool::range:
                laneDrag = LaneDrag::range;
                viewState.setRange ({ laneDragStartTime, laneDragStartTime });
                sm.selectOnly (&track);
                break;

            case Tool::draw:
                if (TrackOps::getKind (track) == TrackKind::instrument || dynamic_cast<te::AudioTrack*> (&track) != nullptr)
                    laneDrag = LaneDrag::drawMidi;
                break;

            default:
                laneDrag = LaneDrag::rubberBand;

                if (! e.mods.isShiftDown() && ! e.mods.isCommandDown())
                {
                    sm.selectOnly (&track);
                    viewState.clearRange();
                }

                if (! edit.getTransport().isPlaying() && ! edit.getTransport().isRecording())
                    edit.getTransport().setPosition (viewState.snap (t));

                break;
        }
    }

    void ArrangeView::laneMouseDrag (te::Track&, const juce::MouseEvent& e)
    {
        auto t = viewState.xToTime (e.x - lanesArea.getX());
        auto snapped = e.mods.isShiftDown() ? t : viewState.snap (t);

        switch (laneDrag)
        {
            case LaneDrag::range:
                viewState.setRange ({ juce::jmin (laneDragStartTime, snapped), juce::jmax (laneDragStartTime, snapped) });
                break;

            case LaneDrag::rubberBand:
            {
                auto r = juce::Rectangle<int> (laneDragStart, e.getPosition()).translated (-lanesArea.getX(), -lanesArea.getY());
                overlay->change ([&] { overlay->rubberBand = r; });
                break;
            }

            case LaneDrag::drawMidi:
            {
                auto a = juce::jmin (laneDragStartTime, snapped), b = juce::jmax (laneDragStartTime, snapped);
                auto lane = getTrackIndexAt (laneDragStart.y);
                if (lane >= 0)
                {
                    auto x1 = (int) viewState.timeToX (a), x2 = (int) viewState.timeToX (b);
                    overlay->change ([&]
                    {
                        overlay->ghosts.clearQuick();
                        overlay->ghosts.add (juce::Rectangle<int> (x1, lanes[lane]->getY() + 1, juce::jmax (2, x2 - x1), lanes[lane]->getHeight() - 2));
                    });
                }
                break;
            }

            case LaneDrag::none:
                break;
        }
    }

    void ArrangeView::laneMouseUp (te::Track&, const juce::MouseEvent& e)
    {
        auto& sm = viewState.selectionManager;

        if (laneDrag == LaneDrag::rubberBand && e.mouseWasDraggedSinceMouseDown())
        {
            auto band = overlay->rubberBand;

            if (! e.mods.isShiftDown() && ! e.mods.isCommandDown())
                sm.deselectAll();

            for (auto lane : lanes)
            {
                if (! band.intersects (lane->getBounds()))
                    continue;

                for (int i = 0; i < lane->getNumChildComponents(); ++i)
                    if (auto cc = dynamic_cast<ClipComponent*> (lane->getChildComponent (i)))
                        if (band.intersects (cc->getBounds().translated (0, lane->getY())))
                            sm.addToSelection (&cc->getClip());
            }
        }
        else if (laneDrag == LaneDrag::range && e.mouseWasDraggedSinceMouseDown())
        {
            // Select every track the range drag covered.
            auto a = getTrackIndexAt (laneDragStart.y), b = getTrackIndexAt (e.y);
            if (a >= 0)
            {
                if (b < 0) b = a;
                sm.deselectAll();
                for (int i = juce::jmin (a, b); i <= juce::jmax (a, b); ++i)
                    sm.addToSelection (&lanes[i]->getTrack());
            }
        }
        else if (laneDrag == LaneDrag::drawMidi && laneDragTrack != nullptr)
        {
            auto t = viewState.xToTime (e.x - lanesArea.getX());
            auto snapped = e.mods.isShiftDown() ? t : viewState.snap (t);
            auto a = juce::jmin (laneDragStartTime, snapped), b = juce::jmax (laneDragStartTime, snapped);

            if (b - a < te::TimeDuration::fromSeconds (0.01))
            {
                // Click without dragging: one bar.
                auto& ts = edit.tempoSequence;
                auto bb = ts.toBarsAndBeats (a);
                a = ts.toTime (te::tempo::BarsAndBeats { bb.bars, {} });
                b = ts.toTime (te::tempo::BarsAndBeats { bb.bars + 1, {} });
            }

            if (auto at = dynamic_cast<te::AudioTrack*> (laneDragTrack.get()))
            {
                edit.getUndoManager().beginNewTransaction ("Create MIDI clip");
                ClipOps::clearRangeForClip (*at, { a, b }, {});

                if (auto mc = at->insertMIDIClip ({ a, b }, &sm))
                {
                    mc->setName (at->getName());
                    mc->setColour (at->getColour());
                }
            }
        }

        overlay->change ([this]
        {
            overlay->rubberBand = {};
            overlay->ghosts.clear();
        });
        laneDrag = LaneDrag::none;
        laneDragTrack = nullptr;
    }

    //==============================================================================
    static juce::PopupMenu createColourMenu (std::function<void (juce::Colour)> apply)
    {
        juce::PopupMenu m;

        for (int i = 0; i < Palette::trackColours.size(); ++i)
        {
            auto c = Palette::trackColours[i];
            juce::PopupMenu::Item item ("Colour " + juce::String (i + 1));
            item.setColour (c);
            item.setAction ([apply, c] { apply (c); });
            m.addItem (item);
        }

        return m;
    }

    void ArrangeView::showClipMenu (te::Clip& clip)
    {
        auto& sm = viewState.selectionManager;
        auto clips = ClipOps::getSelectedClips (sm);
        if (clips.isEmpty())
            clips = { &clip };

        te::Clip::Ptr clipPtr = &clip;
        auto& transport = edit.getTransport();
        auto* um = &edit.getUndoManager();
        const bool allMuted = std::all_of (clips.begin(), clips.end(), [] (te::Clip* c) { return c->isMuted(); });

        juce::PopupMenu m;

        if (auto mc = dynamic_cast<te::MidiClip*> (&clip))
        {
            m.addItem ("Open in Editor", [this, clipPtr] { openClipEditor (*clipPtr); });
            m.addItem ("Loop Clip (repeat when extended)", true, mc->isLooping(), [clipPtr, um]
            {
                um->beginNewTransaction ("Loop clip");
                ClipOps::setLooping (*clipPtr, ! clipPtr->isLooping());
            });
            m.addSeparator();
        }

        m.addItem ("Split at Playhead", clip.getEditTimeRange().contains (transport.getPosition()), false, [this, &transport]
        {
            ClipOps::splitAt (edit, viewState.selectionManager, transport.getPosition());
        });
        m.addItem ("Duplicate", [this, clips, um]
        {
            um->beginNewTransaction ("Duplicate clips");
            auto copies = ClipOps::duplicateClips (edit, clips);
            viewState.selectionManager.deselectAll();
            for (auto c : copies)
                viewState.selectionManager.addToSelection (c);
        });
        m.addItem (allMuted ? "Unmute" : "Mute", [clips, allMuted, um]
        {
            um->beginNewTransaction ("Mute clips");
            ClipOps::setMuted (clips, ! allMuted);
        });
        m.addItem ("Rename...", [this, clipPtr, um]
        {
            auto* aw = new juce::AlertWindow ("Rename Clip", {}, juce::MessageBoxIconType::NoIcon, this);
            aw->addTextEditor ("name", clipPtr->getName());
            aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, clipPtr, um] (int r)
            {
                if (r == 1)
                {
                    um->beginNewTransaction ("Rename clip");
                    clipPtr->setName (aw->getTextEditorContents ("name"));
                }
            }), true);
        });
        m.addSubMenu ("Colour", createColourMenu ([clips, um] (juce::Colour c)
        {
            um->beginNewTransaction ("Clip colour");
            for (auto cl : clips)
                cl->setColour (c);
        }));

        if (auto wc = dynamic_cast<te::WaveAudioClip*> (&clip); wc != nullptr && CompOps::hasTakes (*wc))
        {
            m.addSeparator();
            te::Track::Ptr tr = clip.getTrack();
            const bool showing = tr != nullptr && EditViewState::isShowingTakes (*tr);
            m.addItem (showing ? "Hide Take Lanes" : "Show Take Lanes", tr != nullptr, false, [tr, showing]
            {
                EditViewState::setShowingTakes (*tr, ! showing);
            });

            juce::PopupMenu takes;
            auto files = CompOps::getTakeFiles (*wc);
            auto active = CompOps::getActiveTake (*wc);
            te::Clip::Ptr cp = wc;

            for (int i = 0; i < files.size(); ++i)
                takes.addItem ("Take " + juce::String (i + 1) + "   (" + files[i].getFileName() + ")", true, i == active, [cp, i, um]
                {
                    um->beginNewTransaction ("Choose take");
                    if (auto w = dynamic_cast<te::WaveAudioClip*> (cp.get()))
                        CompOps::setActiveTake (*w, i);
                });

            m.addSubMenu ("Use Take", takes);
            m.addItem ("Unpack Takes to New Tracks", [cp, um]
            {
                um->beginNewTransaction ("Unpack takes");
                if (auto w = dynamic_cast<te::WaveAudioClip*> (cp.get()))
                    CompOps::unpackTakesToTracks (*w);
            });
            m.addItem ("Remove Takes (keep current)", [cp, um]
            {
                um->beginNewTransaction ("Remove takes");
                if (auto w = dynamic_cast<te::WaveAudioClip*> (cp.get()))
                    CompOps::removeTakes (*w);
            });
        }

        if (auto ac = dynamic_cast<te::AudioClipBase*> (&clip))
        {
            m.addSeparator();
            m.addItem (clips.size() > 1 ? "Crossfade Selected Clips" : "Crossfade with Touching Clips", [this, clips, um]
            {
                um->beginNewTransaction ("Crossfade");
                auto made = ClipOps::crossfadeClips (clips, te::TimeDuration::fromSeconds (ClipOps::defaultCrossfadeSeconds));
                edit.engine.getUIBehaviour().showInfoMessage (made > 0 ? "Crossfaded " + juce::String (made) + " join(s) - drag the fade handles to lengthen"
                                                                       : "No touching clips to crossfade");
            });

            juce::PopupMenu gain;
            for (float db : { 6.0f, 3.0f, 1.0f, -1.0f, -3.0f, -6.0f })
                gain.addItem ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB", [clips, db, um]
                {
                    um->beginNewTransaction ("Clip gain");
                    for (auto c : clips)
                        if (auto a = dynamic_cast<te::AudioClipBase*> (c))
                            a->setGainDB (juce::jlimit (-48.0f, 12.0f, a->getGainDB() + db));
                });
            gain.addSeparator();
            gain.addItem ("Reset to 0 dB", [clips, um]
            {
                um->beginNewTransaction ("Reset clip gain");
                for (auto c : clips)
                    if (auto a = dynamic_cast<te::AudioClipBase*> (c))
                        a->setGainDB (0.0f);
            });
            m.addSubMenu ("Clip Gain (now " + juce::String (ac->getGainDB(), 1) + " dB)", gain);

            for (bool fadeIn : { true, false })
            {
                juce::PopupMenu shapes;
                auto current = fadeIn ? ac->getFadeInType() : ac->getFadeOutType();

                for (auto [type, name] : { std::pair { te::AudioFadeCurve::linear,  "Linear" },
                                           std::pair { te::AudioFadeCurve::convex,  "Convex (equal power)" },
                                           std::pair { te::AudioFadeCurve::concave, "Concave" },
                                           std::pair { te::AudioFadeCurve::sCurve,  "S-Curve" } })
                    shapes.addItem (name, true, current == type, [clips, fadeIn, type = type, um]
                    {
                        um->beginNewTransaction ("Fade shape");
                        for (auto c : clips)
                            if (auto a = dynamic_cast<te::AudioClipBase*> (c))
                            {
                                if (fadeIn) a->setFadeInType (type);
                                else        a->setFadeOutType (type);
                            }
                    });

                m.addSubMenu (fadeIn ? "Fade In Shape" : "Fade Out Shape", shapes);
            }

            m.addItem ("Reverse", [clips, um]
            {
                um->beginNewTransaction ("Reverse");
                for (auto c : clips)
                    if (auto a = dynamic_cast<te::AudioClipBase*> (c))
                        a->setIsReversed (! a->getIsReversed());
            });
            m.addItem ("Remove Fades", [clips, um]
            {
                um->beginNewTransaction ("Remove fades");
                for (auto c : clips)
                    if (auto a = dynamic_cast<te::AudioClipBase*> (c))
                    {
                        a->setFadeIn ({});
                        a->setFadeOut ({});
                    }
            });
            m.addItem ("Show in Explorer", [clipPtr]
            {
                if (auto a = dynamic_cast<te::AudioClipBase*> (clipPtr.get()))
                    a->getAudioFile().getFile().revealToUser();
            });
        }

        m.addSeparator();
        m.addItem ("Delete", [this, clips, um]
        {
            um->beginNewTransaction ("Delete clips");
            viewState.selectionManager.deselectAll();
            ClipOps::deleteClips (clips);
        });

        m.showMenuAsync ({});
    }

    void ArrangeView::toggleAutomationLanes (const juce::Array<te::Track*>& tracks)
    {
        for (auto t : tracks)
        {
            if (! AutomationOps::getLaneKeys (*t).isEmpty())
                AutomationOps::setLaneKeys (*t, {});
            else if (auto vol = AutomationOps::getVolumeParameter (*t))
                AutomationOps::showLane (*t, *vol);
        }
    }

    void ArrangeView::openClipEditor (te::Clip& clip)
    {
        if (onOpenClipEditor)
            onOpenClipEditor (clip);
    }

    void ArrangeView::showTrackMenu (te::Track& track)
    {
        te::Track::Ptr t = &track;
        auto* um = &edit.getUndoManager();
        juce::PopupMenu m;

        m.addItem ("Rename", [this, t]
        {
            for (auto h : headers)
                if (&h->getTrack() == t.get())
                    h->startRename();
        });
        m.addSubMenu ("Colour", createColourMenu ([t, um] (juce::Colour c)
        {
            um->beginNewTransaction ("Track colour");
            t->setColour (c);

            if (auto ct = dynamic_cast<te::ClipTrack*> (t.get()))
                for (auto clip : ct->getClips())
                    clip->setColour (c);
        }));
        // Automation lanes
        {
            juce::PopupMenu autoMenu;
            auto shown = AutomationOps::getLaneKeys (track);

            if (auto vol = AutomationOps::getVolumeParameter (track))
                autoMenu.addItem ("Volume", true, shown.contains (AutomationOps::getKey (*vol)), [t, p = te::AutomatableParameter::Ptr (vol)]
                                  { AutomationOps::showLane (*t, *p); });

            juce::PopupMenu all;
            for (auto p : AutomationOps::getAutomatableParameters (track))
            {
                te::AutomatableParameter::Ptr pp = p;
                all.addItem (AutomationOps::getDisplayName (*p) + (p->hasAutomationPoints() ? "  *" : ""), true,
                             shown.contains (AutomationOps::getKey (*p)), [t, pp] { AutomationOps::showLane (*t, *pp); });
            }

            autoMenu.addSubMenu ("Parameter", all);
            autoMenu.addSeparator();
            autoMenu.addItem ("Show all used lanes", [t]
            {
                for (auto p : AutomationOps::getAutomatableParameters (*t))
                    if (p->hasAutomationPoints())
                        AutomationOps::showLane (*t, *p);
            });
            autoMenu.addItem ("Hide all lanes", ! shown.isEmpty(), false, [t] { AutomationOps::setLaneKeys (*t, {}); });
            m.addSubMenu ("Automation", autoMenu);
        }

        m.addSeparator();
        m.addItem ("Move Up", [this, t, um] { um->beginNewTransaction ("Move track"); TrackOps::moveTrack (edit, *t, -1); });
        m.addItem ("Move Down", [this, t, um] { um->beginNewTransaction ("Move track"); TrackOps::moveTrack (edit, *t, 1); });
        m.addItem ("Duplicate Track", [this, t, um] { um->beginNewTransaction ("Duplicate track"); TrackOps::duplicateTrack (edit, *t); });
        m.addSeparator();

        auto addCmd = [this, &m] (const juce::String& name, int cmd)
        {
            m.addItem (name, [this, cmd] { if (onTrackCommand) onTrackCommand (cmd); });
        };
        addCmd ("Add Audio Track", CommandIDs::addAudioTrack);
        addCmd ("Add Instrument Track...", CommandIDs::addInstrumentTrack);
        addCmd ("Add FX Track", CommandIDs::addFxTrack);
        addCmd ("Add Group Track", CommandIDs::addGroupTrack);
        addCmd ("Add Folder Track", CommandIDs::addFolderTrack);

        if (auto parent = track.getParentFolderTrack())
        {
            m.addSeparator();
            m.addItem ("Move out of \"" + parent->getName() + "\"", [this, t, um]
            {
                um->beginNewTransaction ("Move track");
                if (auto p = t->getParentFolderTrack())
                    edit.moveTrack (t, te::TrackInsertPoint (*p, false));
            });
        }

        // Move into a folder / group.
        juce::PopupMenu folders;
        edit.visitAllTracksRecursive ([&] (te::Track& other)
        {
            if (auto ft = dynamic_cast<te::FolderTrack*> (&other); ft != nullptr && ft != t.get() && ! ft->isAChildOf (*t))
                folders.addItem (ft->getName(), [this, t, um, ftPtr = te::Track::Ptr (ft)]
                {
                    um->beginNewTransaction ("Move track into folder");
                    auto subs = ftPtr->getAllSubTracks (false);
                    edit.moveTrack (t, te::TrackInsertPoint (ftPtr.get(), subs.isEmpty() ? nullptr : subs.getLast()));
                });
            return true;
        });

        if (folders.getNumItems() > 0)
            m.addSubMenu ("Move into", folders);

        m.addSeparator();
        m.addItem ("Delete Track", [this, t, um]
        {
            um->beginNewTransaction ("Delete track");
            viewState.selectionManager.deselectAll();
            TrackOps::deleteTracks (edit, { t.get() });
        });

        m.showMenuAsync ({});
    }

    //==============================================================================
    bool ArrangeView::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (auto& f : files)
            if (ClipOps::isImportableAudioFile (juce::File (f)) || ClipOps::isMidiFile (juce::File (f)))
                return true;

        return false;
    }

    void ArrangeView::fileDragMove (const juce::StringArray&, int x, int y)
    {
        auto t = viewState.snap (viewState.xToTime (x - lanesArea.getX()));
        auto idx = getTrackIndexAt (y);

        overlay->change ([&]
        {
            overlay->dropX = (int) viewState.timeToX (t);
            overlay->dropY = idx >= 0 ? lanes[idx]->getY() : -1;
        });
    }

    void ArrangeView::fileDragExit (const juce::StringArray&)
    {
        overlay->change ([this] { overlay->dropX = overlay->dropY = -1; });
    }

    void ArrangeView::filesDropped (const juce::StringArray& files, int x, int y)
    {
        fileDragExit (files);

        auto t = viewState.snap (viewState.xToTime (x - lanesArea.getX()));
        auto* track = dynamic_cast<te::AudioTrack*> (getTrackAt (y));
        juce::Array<juce::File> audio, midi;

        for (auto& f : files)
        {
            juce::File file (f);
            if (ClipOps::isImportableAudioFile (file))  audio.add (file);
            else if (ClipOps::isMidiFile (file))        midi.add (file);
        }

        auto copy = AppContext::get().settings.getBool (SettingKeys::copyImportedFiles, true);
        auto clips = ClipOps::importAudioFiles (session, audio, track, t, copy);

        for (auto& mf : midi)
        {
            auto* target = track;
            if (target == nullptr)
                target = TrackOps::addInstrumentTrack (edit, nullptr, nullptr, getFirstSelectedTrack()).get();

            if (target != nullptr)
            {
                edit.getUndoManager().beginNewTransaction ("Import MIDI");
                if (auto mc = te::createClipFromFile (mf, *target, false))
                {
                    auto len = mc->getPosition().getLength();
                    mc->setStart (t, false, true);
                    juce::ignoreUnused (len);
                    clips.add (mc.get());
                }
            }
        }

        if (clips.size() > 0)
            AppContext::get().engine().getUIBehaviour().showInfoMessage ("Imported " + juce::String (clips.size()) + " file(s)");
    }

    //==============================================================================
    void ArrangeView::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
    {
        if (v == viewState.state)
        {
            if (id == ViewIDs::viewStart || id == ViewIDs::pixelsPerSecond)
            {
                lastPlayheadX = -1e9;
                updateScrollBars();
                ruler.repaint();
                arranger->repaint();
                overlay->repaint();
            }
            else if (id == ViewIDs::scrollY)
            {
                markAndUpdate (needsLayout);
            }
            else if (id == ViewIDs::snapEnabled || id == ViewIDs::gridIndex)
            {
                arranger->repaint();
            }

            return;
        }

        if (te::TrackList::isTrack (v))
        {
            if (id == ViewIDs::cdawHeight || id == ViewIDs::cdawShowTakes)
                markAndUpdate (needsLayout);
            else if (id == ViewIDs::cdawCollapsed || id == ViewIDs::cdawAutoLanes)
                markAndUpdate (needsRebuild);
            else if (id == te::IDs::colour || id == te::IDs::name)
                repaint();
        }
    }

    void ArrangeView::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
    {
        if (te::TrackList::isTrack (child))
            markAndUpdate (needsRebuild);
    }

    void ArrangeView::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
    {
        if (te::TrackList::isTrack (child))
            markAndUpdate (needsRebuild);
    }

    void ArrangeView::valueTreeChildOrderChanged (juce::ValueTree& parent, int a, int b)
    {
        if (te::TrackList::isTrack (parent.getChild (a)) || te::TrackList::isTrack (parent.getChild (b)))
            markAndUpdate (needsRebuild);
    }

    void ArrangeView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // Lanes, headers and clips repaint themselves when their own selected state changes.
    }

    void ArrangeView::handleAsyncUpdate()
    {
        if (compareAndReset (needsRebuild))
        {
            needsLayout = false;
            buildTracks();
            repaint();
        }

        if (compareAndReset (needsLayout))
            layoutTracks();
    }
}
