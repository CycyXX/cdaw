#include "TimeRuler.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/TempoOps.h"
#include "../../Model/ArrangerOps.h"

namespace cdaw
{
    namespace GridPainter
    {
        void forEachBar (const EditViewState& vs, double widthPx, const std::function<void (int, te::TimePosition)>& fn)
        {
            forEachBar (vs, 0.0, widthPx, fn);
        }

        void forEachBar (const EditViewState& vs, double fromX, double toX, const std::function<void (int, te::TimePosition)>& fn)
        {
            auto& ts = vs.edit.tempoSequence;
            auto t0 = vs.xToTime (juce::jmax (0.0, fromX));
            auto t1 = vs.xToTime (toX);

            auto bar = ts.toBarsAndBeats (t0).bars;

            for (int guard = 0; guard < 10000; ++guard, ++bar)
            {
                auto t = ts.toTime (te::tempo::BarsAndBeats { bar, {} });

                if (t > t1)
                    break;

                fn (bar + 1, t);
            }
        }

        double pixelsPerBar (const EditViewState& vs)
        {
            auto& ts = vs.edit.tempoSequence;
            auto bar = ts.toBarsAndBeats (vs.xToTime (0.0)).bars;
            auto t0 = ts.toTime (te::tempo::BarsAndBeats { bar, {} });
            auto t1 = ts.toTime (te::tempo::BarsAndBeats { bar + 1, {} });
            return vs.timeToX (t1) - vs.timeToX (t0);
        }

        int barStep (double pxPerBar, double minPixels)
        {
            int step = 1;

            while (step < 4096 && pxPerBar * step < minPixels)
                step *= 2;

            return step;
        }

        void drawGridLines (juce::Graphics& g, const EditViewState& vs, juce::Rectangle<int> area)
        {
            auto& ts = vs.edit.tempoSequence;
            const auto width = (double) area.getWidth();
            const auto top = (float) area.getY();
            const auto bottom = (float) area.getBottom();
            const auto grid = vs.getGrid();

            // Zoomed out: only every Nth bar gets a line, so the lanes don't turn into a solid grey wash.
            const auto pxPerBar = pixelsPerBar (vs);
            const int lineStep = barStep (pxPerBar, 16.0);
            const int strongStep = barStep (pxPerBar, 48.0);

            // Only the bars inside the area being repainted (e.g. a thin playhead strip).
            auto dirty = g.getClipBounds().getIntersection (area);
            if (dirty.isEmpty())
                return;

            const double fromX = dirty.getX() - area.getX() - 2.0;
            const double toX = dirty.getRight() - area.getX() + 2.0;
            juce::ignoreUnused (width);

            forEachBar (vs, fromX, toX, [&] (int barNumber, te::TimePosition barStart)
            {
                if (lineStep > 1)
                {
                    if ((barNumber - 1) % lineStep == 0)
                    {
                        const bool strong = (barNumber - 1) % strongStep == 0;
                        g.setColour (strong ? Palette::gridBar : Palette::gridBeat);
                        g.drawVerticalLine ((int) (area.getX() + vs.timeToX (barStart)), top, bottom);
                    }

                    return;
                }

                auto& sig = ts.getTimeSigAt (barStart);
                const int num = juce::jmax (1, sig.numerator.get());
                const int den = juce::jmax (1, sig.denominator.get());
                auto barBeats = ts.toBeats (barStart);

                auto beatToX = [&] (double beatsFromBar)
                {
                    return area.getX() + vs.timeToX (ts.toTime (barBeats + te::BeatDuration::fromBeats (beatsFromBar)));
                };

                const double pxPerBeat = beatToX (1.0) - beatToX (0.0);

                if (grid.wholeNotes > 0.0)
                {
                    const double gridBeats = grid.wholeNotes * den;
                    if (gridBeats < 1.0 && pxPerBeat * gridBeats > 7.0)
                    {
                        g.setColour (Palette::gridSub);
                        for (double b = gridBeats; b < num - 1e-6; b += gridBeats)
                            if (std::fmod (b, 1.0) > 1e-6 && std::fmod (b, 1.0) < 1.0 - 1e-6)
                                g.drawVerticalLine ((int) beatToX (b), top, bottom);
                    }
                }

                if (pxPerBeat > 10.0)
                {
                    g.setColour (Palette::gridBeat);
                    for (int b = 1; b < num; ++b)
                        g.drawVerticalLine ((int) beatToX ((double) b), top, bottom);
                }

                if (pxPerBeat * num > 4.0)
                {
                    g.setColour (Palette::gridBar);
                    g.drawVerticalLine ((int) (area.getX() + vs.timeToX (barStart)), top, bottom);
                }
            });

            // Song section starts through the lanes, in the section's colour.
            for (auto s : ArrangerOps::getSections (vs.edit))
            {
                auto x = (float) (area.getX() + vs.timeToX (s->getPosition().getStart()));
                if (x >= dirty.getX() - 2 && x <= dirty.getRight() + 2)
                {
                    g.setColour (s->getColour().withAlpha (0.45f));
                    g.fillRect (x - 0.5f, top, 2.0f, bottom - top);
                }
            }

            // Marker lines through the lanes
            for (auto m : vs.edit.getMarkerManager().getMarkers())
            {
                auto x = (float) (area.getX() + vs.timeToX (m->getPosition().getStart()));
                if (x >= dirty.getX() - 2 && x <= dirty.getRight() + 2)
                {
                    g.setColour (Palette::marker.withAlpha (0.35f));
                    g.drawVerticalLine ((int) x, top, bottom);
                }
            }
        }
    }

    //==============================================================================
    bool TimeRuler::affectsRuler (const juce::ValueTree& v, const juce::Identifier& id)
    {
        // What the ruler draws: markers, tempo and signature changes, the loop / punch range.
        // (Scrolling and zooming are repainted by the arrange view; the play position by its playhead.)
        if (v.hasType (te::IDs::TRANSPORT))
            return id == te::IDs::loopPoint1 || id == te::IDs::loopPoint2 || id == te::IDs::looping;

        if (id == te::IDs::recordPunchInOut)
            return true;

        for (auto t = v; t.isValid(); t = t.getParent())
            if (t.hasType (te::IDs::MARKERCLIP) || t.hasType (te::IDs::MARKERTRACK) || t.hasType (te::IDs::TEMPOSEQUENCE))
                return true;

        return false;
    }

    TimeRuler::TimeRuler (EditViewState& vs) : viewState (vs), edit (vs.edit)
    {
        setOpaque (true);   // paint() fills the whole component
        edit.state.addListener (this);
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }

    TimeRuler::~TimeRuler()
    {
        edit.state.removeListener (this);
    }

    std::vector<TimeRuler::Item> TimeRuler::layoutItems() const
    {
        std::vector<Item> items;
        auto font = juce::Font (juce::FontOptions (11.0f));
        auto& ts = edit.tempoSequence;

        auto add = [&] (Item::Kind kind, te::TimePosition t, const juce::String& text, te::MarkerClip* marker, int index)
        {
            auto x = (int) viewState.timeToX (t);
            auto w = juce::GlyphArrangement::getStringWidthInt (font, text) + 12;

            if (x + w < 0 || x > getWidth())
                return;

            items.push_back ({ kind, { x, 1, w, markerZoneHeight - 2 }, marker, index });
        };

        for (auto m : edit.getMarkerManager().getMarkers())
            add (Item::Kind::marker, m->getPosition().getStart(), m->getName(), m, -1);

        for (int i = 1; i < ts.getNumTempos(); ++i)
            if (auto* tempo = ts.getTempo (i))
                add (Item::Kind::tempo, tempo->getStartTime(), juce::String (tempo->getBpm(), 1), nullptr, i);

        for (int i = 1; i < ts.getNumTimeSigs(); ++i)
            if (auto* sig = ts.getTimeSig (i))
                add (Item::Kind::timeSig, ts.toTime (sig->getStartBeat()), sig->getStringTimeSig(), nullptr, i);

        return items;
    }

    const std::vector<TimeRuler::Item>& TimeRuler::getItems() const
    {
        // Rebuild when markers / tempo / signatures changed, or the view scrolled, zoomed or resized.
        if (itemsDirty || cachedViewStart != viewState.viewStart.get() || cachedZoom != viewState.pixelsPerSecond.get()
             || cachedWidth != getWidth())
        {
            itemCache = layoutItems();
            itemsDirty = false;
            cachedViewStart = viewState.viewStart.get();
            cachedZoom = viewState.pixelsPerSecond.get();
            cachedWidth = getWidth();
        }

        return itemCache;
    }

    const TimeRuler::Item* TimeRuler::itemAt (juce::Point<int> p) const
    {
        auto& cache = getItems();

        for (auto it = cache.rbegin(); it != cache.rend(); ++it)
            if (it->bounds.contains (p))
                return &*it;

        return nullptr;
    }

    void TimeRuler::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        auto& transport = edit.getTransport();
        auto& ts = edit.tempoSequence;

        // Marker / tempo strip
        g.setColour (Palette::panelBackground.darker (0.25f));
        g.fillRect (0, 0, getWidth(), markerZoneHeight);
        g.setFont (juce::FontOptions (11.0f));

        for (auto& item : getItems())
        {
            auto colour = item.kind == Item::Kind::marker ? Palette::marker : Palette::tempo;
            juce::String text;

            if (item.kind == Item::Kind::marker)       text = item.marker->getName();
            else if (item.kind == Item::Kind::tempo)   text = juce::String (ts.getTempo (item.index)->getBpm(), 1);
            else                                       text = ts.getTimeSig (item.index)->getStringTimeSig();

            g.setColour (colour.withAlpha (0.3f));
            g.fillRect (item.bounds);
            g.setColour (colour);
            g.fillRect (item.bounds.getX(), 0, 2, markerZoneHeight);
            g.setColour (Palette::text);
            g.drawText (text, item.bounds.withTrimmedLeft (5), juce::Justification::centredLeft, false);
        }

        const int loopTop = markerZoneHeight;
        const int barsTop = markerZoneHeight + loopZoneHeight;

        // Loop / punch range
        auto loop = transport.getLoopRange();

        if (! loop.isEmpty())
        {
            auto x1 = (float) viewState.timeToX (loop.getStart());
            auto x2 = (float) viewState.timeToX (loop.getEnd());
            auto active = transport.looping.get() || edit.recordingPunchInOut.get();
            auto colour = edit.recordingPunchInOut.get() ? Palette::record : Palette::accent;

            g.setColour (colour.withAlpha (active ? 0.45f : 0.15f));
            g.fillRect (juce::Rectangle<float> (x1, (float) loopTop, x2 - x1, (float) loopZoneHeight));
            g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
            g.drawVerticalLine ((int) x1, (float) loopTop, (float) getHeight());
            g.drawVerticalLine ((int) x2, (float) loopTop, (float) getHeight());
        }

        // Bar numbers and ticks. When zoomed out, labelled bars (1, 5, 9 ... or 1, 9, 17 ...) get a
        // full-height line, a few in-between bars get a short tick, and the rest are skipped.
        g.setFont (juce::FontOptions (12.0f));
        const auto pxPerBar = GridPainter::pixelsPerBar (viewState);
        const int labelStep = GridPainter::barStep (pxPerBar, 44.0);
        const int tickStep = GridPainter::barStep (pxPerBar, 7.0);
        const auto rulerH = (float) (getHeight() - barsTop);
        int lastLabelX = -1000;

        // Bars inside the repainted area, plus a margin so label spacing matches a full paint.
        auto dirty = g.getClipBounds();
        GridPainter::forEachBar (viewState, dirty.getX() - 80.0, dirty.getRight() + 4.0, [&] (int barNumber, te::TimePosition t)
        {
            const int index = barNumber - 1;

            if (index % tickStep != 0)
                return;

            auto x = (int) viewState.timeToX (t);
            const bool labelled = index % labelStep == 0;

            if (labelled)
            {
                g.setColour (Palette::textDim);
                g.drawVerticalLine (x, (float) barsTop, (float) getHeight());

                if (x - lastLabelX > 30)
                {
                    g.setColour (Palette::text);
                    g.drawText (juce::String (barNumber), x + 3, barsTop, 44, getHeight() - barsTop - 4, juce::Justification::centredLeft, false);
                    lastLabelX = x;
                }
            }
            else
            {
                // Halfway marks between labels are a bit longer than the rest.
                const bool half = labelStep >= 2 && index % (labelStep / 2) == 0;
                g.setColour (Palette::textDim.withAlpha (half ? 0.8f : 0.45f));
                g.drawVerticalLine (x, (float) getHeight() - rulerH * (half ? 0.45f : 0.25f), (float) getHeight());
            }

            // Beat ticks only when there's room for them.
            if (labelStep == 1)
            {
                auto num = juce::jmax (1, ts.getTimeSigAt (t).numerator.get());
                auto barBeats = ts.toBeats (t);
                auto pxPerBeat = pxPerBar / num;

                if (pxPerBeat > 8.0)
                {
                    g.setColour (Palette::textDim.withAlpha (0.6f));
                    for (int b = 1; b < num; ++b)
                    {
                        auto bx = (int) viewState.timeToX (ts.toTime (barBeats + te::BeatDuration::fromBeats ((double) b)));
                        g.drawVerticalLine (bx, getHeight() - 5.0f, (float) getHeight());
                    }
                }
            }
        });

        // Playhead marker
        auto px = (float) viewState.timeToX (transport.getPosition());
        juce::Path tri;
        tri.addTriangle (px - 5.0f, (float) getHeight() - 8.0f, px + 5.0f, (float) getHeight() - 8.0f, px, (float) getHeight());
        g.setColour (Palette::playhead);
        g.fillPath (tri);

        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.drawHorizontalLine (markerZoneHeight, 0.0f, (float) getWidth());
    }

    void TimeRuler::showItemMenu (const Item& item)
    {
        auto& um = edit.getUndoManager();
        juce::PopupMenu m;

        if (item.kind == Item::Kind::marker)
        {
            te::MarkerClip::Ptr marker = item.marker;
            m.addItem ("Rename...", [this, marker]
            {
                auto* aw = new juce::AlertWindow ("Rename Marker", {}, juce::MessageBoxIconType::NoIcon, this);
                aw->addTextEditor ("name", marker->getName());
                aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, marker, this] (int r)
                {
                    if (r == 1)
                    {
                        edit.getUndoManager().beginNewTransaction ("Rename marker");
                        marker->setName (aw->getTextEditorContents ("name"));
                    }
                }), true);
            });
            m.addItem ("Delete Marker", [this, marker, &um]
            {
                um.beginNewTransaction ("Delete marker");
                marker->removeFromParent();
                repaint();
            });
        }
        else if (item.kind == Item::Kind::tempo)
        {
            auto index = item.index;
            m.addItem ("Delete Tempo Change", [this, index, &um]
            {
                um.beginNewTransaction ("Delete tempo change");
                edit.tempoSequence.removeTempo (index, true);
            });
        }
        else
        {
            auto index = item.index;
            m.addItem ("Delete Time Signature Change", [this, index, &um]
            {
                um.beginNewTransaction ("Delete time signature");
                edit.tempoSequence.removeTimeSig (index);
            });
        }

        m.showMenuAsync ({});
    }

    void TimeRuler::showStripMenu (te::TimePosition t)
    {
        juce::PopupMenu m;
        auto snapped = viewState.snap (t);

        m.addItem ("Add Marker Here", [this, snapped]
        {
            edit.getUndoManager().beginNewTransaction ("Add marker");
            TempoOps::addMarker (edit, snapped);
        });
        m.addItem ("Insert Tempo Change Here...", [this, snapped]
        {
            auto* aw = new juce::AlertWindow ("Tempo Change", "New tempo (BPM) from this beat:", juce::MessageBoxIconType::NoIcon, this);
            aw->addTextEditor ("bpm", juce::String (edit.tempoSequence.getTempoAt (snapped).getBpm(), 2));
            aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, snapped] (int r)
            {
                if (r == 1)
                {
                    edit.getUndoManager().beginNewTransaction ("Tempo change");
                    TempoOps::setTempoAt (edit, snapped, aw->getTextEditorContents ("bpm").getDoubleValue());
                }
            }), true);
        });
        m.addItem ("Insert Time Signature Here...", [this, snapped]
        {
            auto* aw = new juce::AlertWindow ("Time Signature", "Time signature from this bar (e.g. 7/8):", juce::MessageBoxIconType::NoIcon, this);
            aw->addTextEditor ("sig", edit.tempoSequence.getTimeSigAt (snapped).getStringTimeSig());
            aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, snapped] (int r)
            {
                if (r == 1)
                {
                    edit.getUndoManager().beginNewTransaction ("Time signature change");
                    TempoOps::setTimeSigAt (edit, snapped, aw->getTextEditorContents ("sig"));
                }
            }), true);
        });

        m.showMenuAsync ({});
    }

    void TimeRuler::mouseDown (const juce::MouseEvent& e)
    {
        auto t = viewState.snap (viewState.xToTime (e.x));
        auto& transport = edit.getTransport();

        if (e.y < markerZoneHeight)
        {
            if (auto* item = itemAt (e.getPosition()))
            {
                if (e.mods.isPopupMenu())
                {
                    auto copy = *item;
                    showItemMenu (copy);
                }
                else if (item->kind == Item::Kind::marker)
                {
                    transport.setPosition (item->marker->getPosition().getStart());
                }
                return;
            }

            if (e.mods.isPopupMenu())
                showStripMenu (viewState.xToTime (e.x));

            return;
        }

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("Loop (cycle)", true, transport.looping.get(), [&transport] { transport.looping = ! transport.looping.get(); });
            m.addItem ("Punch in/out on loop range", true, edit.recordingPunchInOut.get(), [this] { edit.recordingPunchInOut = ! edit.recordingPunchInOut.get(); });
            m.addItem ("Set loop to selected range", viewState.hasRange(), false, [this, &transport] { transport.setLoopRange (viewState.getRange()); });
            m.addSeparator();
            m.addItem ("Add Marker at Playhead", [this]
            {
                edit.getUndoManager().beginNewTransaction ("Add marker");
                TempoOps::addMarker (edit, edit.getTransport().getPosition());
            });
            m.showMenuAsync ({});
            return;
        }

        if (e.y < markerZoneHeight + loopZoneHeight)
        {
            draggingLoop = true;
            loopDragStart = t;
        }
        else
        {
            draggingPlayhead = true;
            transport.setUserDragging (true);
            transport.setPosition (t);
        }
    }

    void TimeRuler::mouseDrag (const juce::MouseEvent& e)
    {
        auto t = viewState.snap (viewState.xToTime (e.x));

        if (draggingLoop && e.getDistanceFromDragStartX() != 0)
        {
            auto a = juce::jmin (loopDragStart, t), b = juce::jmax (loopDragStart, t);

            if (b > a)
                edit.getTransport().setLoopRange ({ a, b });

            repaint();
        }
        else if (draggingPlayhead)
        {
            edit.getTransport().setPosition (t);
            repaint();
        }
    }

    void TimeRuler::mouseUp (const juce::MouseEvent& e)
    {
        if (draggingLoop && e.mouseWasDraggedSinceMouseDown())
            edit.getTransport().looping = true;

        if (draggingPlayhead)
            edit.getTransport().setUserDragging (false);

        draggingLoop = draggingPlayhead = false;
        repaint();
    }

    void TimeRuler::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.y < markerZoneHeight)
        {
            if (auto* item = itemAt (e.getPosition()); item != nullptr && item->kind == Item::Kind::marker)
            {
                auto copy = *item;
                showItemMenu (copy);
            }
            else
            {
                edit.getUndoManager().beginNewTransaction ("Add marker");
                TempoOps::addMarker (edit, viewState.snap (viewState.xToTime (e.x)));
            }
        }
        else if (e.y < markerZoneHeight + loopZoneHeight)
        {
            auto& transport = edit.getTransport();
            transport.looping = ! transport.looping.get();
            repaint();
        }
    }
}
