#include "TimeRuler.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/TempoOps.h"

namespace cdaw
{
    namespace GridPainter
    {
        void forEachBar (const EditViewState& vs, double widthPx, const std::function<void (int, te::TimePosition)>& fn)
        {
            auto& ts = vs.edit.tempoSequence;
            auto t0 = vs.xToTime (0.0);
            auto t1 = vs.xToTime (widthPx);

            auto bar = ts.toBarsAndBeats (t0).bars;

            for (int guard = 0; guard < 10000; ++guard, ++bar)
            {
                auto t = ts.toTime (te::tempo::BarsAndBeats { bar, {} });

                if (t > t1)
                    break;

                fn (bar + 1, t);
            }
        }

        void drawGridLines (juce::Graphics& g, const EditViewState& vs, juce::Rectangle<int> area)
        {
            auto& ts = vs.edit.tempoSequence;
            const auto width = (double) area.getWidth();
            const auto top = (float) area.getY();
            const auto bottom = (float) area.getBottom();
            const auto grid = vs.getGrid();

            forEachBar (vs, width, [&] (int, te::TimePosition barStart)
            {
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

            // Marker lines through the lanes
            for (auto m : vs.edit.getMarkerManager().getMarkers())
            {
                auto x = (float) (area.getX() + vs.timeToX (m->getPosition().getStart()));
                if (x >= area.getX() && x <= area.getRight())
                {
                    g.setColour (Palette::marker.withAlpha (0.35f));
                    g.drawVerticalLine ((int) x, top, bottom);
                }
            }
        }
    }

    //==============================================================================
    TimeRuler::TimeRuler (EditViewState& vs) : viewState (vs), edit (vs.edit)
    {
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

    const TimeRuler::Item* TimeRuler::itemAt (juce::Point<int> p) const
    {
        static std::vector<Item> cache;
        cache = layoutItems();

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

        for (auto& item : layoutItems())
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

        // Bar numbers and ticks
        g.setFont (juce::FontOptions (12.0f));
        int lastLabelX = -1000;

        GridPainter::forEachBar (viewState, getWidth(), [&] (int barNumber, te::TimePosition t)
        {
            auto x = (int) viewState.timeToX (t);
            auto num = juce::jmax (1, ts.getTimeSigAt (t).numerator.get());
            auto barBeats = ts.toBeats (t);
            auto nextBeatX = viewState.timeToX (ts.toTime (barBeats + te::BeatDuration::fromBeats (1.0)));
            auto pxPerBeat = nextBeatX - x;

            if (pxPerBeat * num < 3.0)
                return;

            g.setColour (Palette::textDim);
            g.drawVerticalLine (x, (float) barsTop, (float) getHeight());

            if (pxPerBeat > 8.0)
                for (int b = 1; b < num; ++b)
                {
                    auto bx = (int) viewState.timeToX (ts.toTime (barBeats + te::BeatDuration::fromBeats ((double) b)));
                    g.drawVerticalLine (bx, getHeight() - 5.0f, (float) getHeight());
                }

            if (x - lastLabelX > 34)
            {
                g.setColour (Palette::text);
                g.drawText (juce::String (barNumber), x + 3, barsTop, 40, getHeight() - barsTop, juce::Justification::centredLeft, false);
                lastLabelX = x;
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
