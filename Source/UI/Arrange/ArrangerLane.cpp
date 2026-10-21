#include "ArrangerLane.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/ArrangerOps.h"

namespace cdaw
{
    ArrangerLane::ArrangerLane (EditViewState& vs, int headerW)
        : viewState (vs), edit (vs.edit), headerWidth (headerW)
    {
        setOpaque (true);   // paint() fills the whole component
        if (auto track = ArrangerOps::getTrack (edit))
        {
            trackState = track->state;
            trackState.addListener (this);
        }

        addButton.setTooltip ("Add a song section at the playhead (or over the loop range)");
        addButton.onClick = [this] { addSectionAtPlayhead(); };
        addAndMakeVisible (addButton);

        setTooltip ("Song sections. Drag to create, drag a section to move it with all its tracks "
                    "(Alt-drag copies), double-click to rename, right-click for more.");
    }

    ArrangerLane::~ArrangerLane()
    {
        trackState.removeListener (this);
    }

    void ArrangerLane::changed (bool affectsLanes)
    {
        repaint();

        if (affectsLanes && onSectionsChanged)
            onSectionsChanged();
    }

    void ArrangerLane::resized()
    {
        addButton.setBounds (headerWidth - 28, 3, 22, height - 6);
    }

    //==============================================================================
    juce::Rectangle<int> ArrangerLane::getSectionBounds (te::ArrangerClip& s) const
    {
        auto x1 = (int) timeToX (s.getPosition().getStart());
        auto x2 = (int) timeToX (s.getPosition().getEnd());
        return { x1, 3, juce::jmax (2, x2 - x1), getHeight() - 6 };
    }

    te::ArrangerClip* ArrangerLane::sectionAt (int x) const
    {
        if (x < headerWidth)
            return nullptr;

        for (auto s : ArrangerOps::getSections (edit))
            if (x >= (int) timeToX (s->getPosition().getStart()) && x < (int) timeToX (s->getPosition().getEnd()))
                return s;

        return nullptr;
    }

    te::TimePosition ArrangerLane::snapToBar (te::TimePosition t) const
    {
        if (! viewState.snapEnabled.get())
            return t;

        auto& ts = edit.tempoSequence;
        auto bars = ts.toBarsAndBeats (t).bars;
        auto barStart = ts.toTime (te::tempo::BarsAndBeats { bars, {} });
        auto nextBar = ts.toTime (te::tempo::BarsAndBeats { bars + 1, {} });
        return (t - barStart) < (nextBar - t) ? barStart : nextBar;
    }

    te::TimePosition ArrangerLane::findDropTime (int x) const
    {
        // Prefer section boundaries (that's where sections go); otherwise the nearest bar.
        auto t = xToTime (x);
        te::TimePosition best = snapToBar (t);
        double bestDistance = 14.0;

        for (auto s : ArrangerOps::getSections (edit))
        {
            for (auto edgeTime : { s->getPosition().getStart(), s->getPosition().getEnd() })
            {
                auto d = std::abs (timeToX (edgeTime) - x);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = edgeTime;
                }
            }
        }

        return best;
    }

    te::TimeRange ArrangerLane::rangeForNewSection (te::TimePosition at) const
    {
        if (ArrangerOps::findSectionAt (edit, at) != nullptr)
            return {};

        auto& ts = edit.tempoSequence;
        auto bars = ts.toBarsAndBeats (at).bars;
        auto start = ts.toTime (te::tempo::BarsAndBeats { bars, {} });
        auto end = ts.toTime (te::tempo::BarsAndBeats { bars + 8, {} });

        // Stop at the next section, and start after the previous one.
        for (auto s : ArrangerOps::getSections (edit))
        {
            if (s->getPosition().getStart() >= at)
                end = juce::jmin (end, s->getPosition().getStart());
            if (s->getPosition().getEnd() <= at)
                start = juce::jmax (start, s->getPosition().getEnd());
        }

        return end > start ? te::TimeRange (start, end) : te::TimeRange();
    }

    juce::String ArrangerLane::suggestName (te::TimePosition at) const
    {
        // Follow the usual song order from the section before; number repeats ("Verse 2").
        juce::String previous;
        for (auto s : ArrangerOps::getSections (edit))
            if (s->getPosition().getStart() < at)
                previous = s->getName().trimCharactersAtEnd ("0123456789 ");

        juce::String base = "Section";

        if (previous.isEmpty())                     base = "Intro";
        else if (previous == "Intro")               base = "Verse";
        else if (previous == "Verse")               base = "Chorus";
        else if (previous == "Pre-Chorus")          base = "Chorus";
        else if (previous == "Chorus")              base = "Verse";
        else if (previous == "Post-Chorus")         base = "Verse";
        else if (previous == "Bridge" || previous == "Solo" || previous == "Breakdown") base = "Chorus";

        int count = 0;
        for (auto s : ArrangerOps::getSections (edit))
            if (s->getName().trimCharactersAtEnd ("0123456789 ") == base)
                ++count;

        return count == 0 ? base : base + " " + juce::String (count + 1);
    }

    //==============================================================================
    void ArrangerLane::paint (juce::Graphics& g)
    {
        // Header cell
        auto header = getLocalBounds().removeFromLeft (headerWidth);
        g.setColour (Palette::panelBackground);
        g.fillRect (header);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Arranger", header.reduced (10, 0), juce::Justification::centredLeft);

        auto area = getLocalBounds().withTrimmedLeft (headerWidth);
        g.setColour (Palette::laneAlternate);
        g.fillRect (area);

        g.reduceClipRegion (area);
        auto& ts = edit.tempoSequence;

        for (auto s : ArrangerOps::getSections (edit))
        {
            auto b = getSectionBounds (*s);

            if (b.getRight() < area.getX() || b.getX() > area.getRight())
                continue;

            const bool selected = s == selectedSection.get();
            const bool beingMoved = drag == Drag::move && s == dragSection.get() && dragX >= 0;
            auto colour = s->getColour();

            g.setColour (colour.withAlpha (beingMoved && ! dragCopy ? 0.35f : 0.85f));
            g.fillRoundedRectangle (b.toFloat(), 4.0f);
            g.setColour (selected ? Palette::text : colour.darker (0.5f));
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);

            // Name, plus the length in bars when there's room. Keep it readable when scrolled.
            auto textArea = b.reduced (7, 0);
            if (textArea.getX() < area.getX() + 4)
                textArea.setLeft (area.getX() + 4);

            auto bars = ts.toBarsAndBeats (s->getPosition().getEnd()).getTotalBars()
                        - ts.toBarsAndBeats (s->getPosition().getStart()).getTotalBars();
            auto ink = colour.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white;

            g.setColour (ink);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (s->getName(), textArea, juce::Justification::centredLeft, true);

            auto nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s->getName());
            if (textArea.getWidth() > nameWidth + 60)
            {
                g.setColour (ink.withAlpha (0.6f));
                g.setFont (juce::FontOptions (10.5f));
                auto barsText = std::abs (bars - std::round (bars)) < 0.05 ? juce::String (juce::roundToInt (bars)) : juce::String (bars, 1);
                g.drawText (barsText + (barsText == "1" ? " bar" : " bars"),
                            textArea.withTrimmedLeft (nameWidth + 8), juce::Justification::centredLeft, false);
            }
        }

        // Creating: the range being drawn.
        if (drag == Drag::create && ! createRange.isEmpty())
        {
            auto x1 = (float) timeToX (createRange.getStart()), x2 = (float) timeToX (createRange.getEnd());
            auto r = juce::Rectangle<float> (x1, 3.0f, x2 - x1, (float) getHeight() - 6.0f);
            g.setColour (Palette::accent.withAlpha (0.3f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::accent);
            g.drawRoundedRectangle (r, 4.0f, 1.5f);
        }

        // Moving: a ghost under the mouse and where it will land.
        if (drag == Drag::move && dragSection != nullptr && dragX >= 0)
        {
            auto b = getSectionBounds (*dragSection).toFloat();
            auto ghost = b.withX ((float) dragX - b.getWidth() * 0.5f);
            g.setColour (dragSection->getColour().withAlpha (0.55f));
            g.fillRoundedRectangle (ghost, 4.0f);
            g.setColour (Palette::text);
            g.drawRoundedRectangle (ghost, 4.0f, 1.0f);

            if (dropX >= 0)
            {
                g.setColour (Palette::accent);
                g.fillRect (dropX - 1, 0, 3, getHeight());
            }
        }

        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    //==============================================================================
    void ArrangerLane::mouseMove (const juce::MouseEvent& e)
    {
        auto cursor = juce::MouseCursor::NormalCursor;

        if (auto s = sectionAt (e.x))
        {
            auto b = getSectionBounds (*s);
            cursor = (e.x - b.getX() < 6 || b.getRight() - e.x < 6) ? juce::MouseCursor::LeftRightResizeCursor
                                                                    : juce::MouseCursor::DraggingHandCursor;
        }
        else if (e.x >= headerWidth)
        {
            // Edge of the section to the left (its end is just outside it).
            for (auto s2 : ArrangerOps::getSections (edit))
                if (std::abs (timeToX (s2->getPosition().getEnd()) - e.x) < 6)
                    cursor = juce::MouseCursor::LeftRightResizeCursor;
        }

        setMouseCursor (cursor);
    }

    void ArrangerLane::mouseExit (const juce::MouseEvent&)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void ArrangerLane::mouseDown (const juce::MouseEvent& e)
    {
        nameEditor.reset();
        drag = Drag::none;
        dragX = dropX = -1;

        if (e.x < headerWidth)
            return;

        auto t = xToTime (e.x);
        te::ArrangerClip* section = sectionAt (e.x);

        // A section's end edge sits just outside it: still grab it for resizing.
        if (section == nullptr)
            for (auto s : ArrangerOps::getSections (edit))
                if (std::abs (timeToX (s->getPosition().getEnd()) - e.x) < 6)
                    section = s;

        if (e.mods.isPopupMenu())
        {
            if (section != nullptr)
            {
                selectedSection = section;
                repaint();
                showSectionMenu (*section);
            }
            else
            {
                showEmptyMenu (t);
            }
            return;
        }

        if (section != nullptr)
        {
            selectedSection = section;
            dragSection = section;
            auto b = getSectionBounds (*section);

            if (std::abs (e.x - b.getX()) < 6)              drag = Drag::resizeStart;
            else if (std::abs (e.x - b.getRight()) < 6)     drag = Drag::resizeEnd;
            else                                            drag = Drag::move;

            if (drag != Drag::move)
                edit.getUndoManager().beginNewTransaction ("Resize section");
        }
        else
        {
            selectedSection = nullptr;
            drag = Drag::create;
            dragStartTime = snapToBar (t);
            createRange = {};
        }

        repaint();
    }

    void ArrangerLane::mouseDrag (const juce::MouseEvent& e)
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            return;

        auto t = snapToBar (xToTime (e.x));

        switch (drag)
        {
            case Drag::create:
                createRange = { juce::jmin (dragStartTime, t), juce::jmax (dragStartTime, t) };
                break;

            case Drag::move:
                dragX = e.x;
                dragCopy = e.mods.isAltDown();
                dropX = (int) timeToX (findDropTime (e.x));
                break;

            case Drag::resizeStart:
                if (dragSection != nullptr)
                    ArrangerOps::resizeSection (*dragSection, { t, dragSection->getPosition().getEnd() });
                break;

            case Drag::resizeEnd:
                if (dragSection != nullptr)
                    ArrangerOps::resizeSection (*dragSection, { dragSection->getPosition().getStart(), t });
                break;

            case Drag::none:
                break;
        }

        repaint();
    }

    void ArrangerLane::mouseUp (const juce::MouseEvent& e)
    {
        auto& um = edit.getUndoManager();
        auto& transport = edit.getTransport();

        if (drag == Drag::create && e.mouseWasDraggedSinceMouseDown()
             && createRange.getLength() > te::TimeDuration::fromSeconds (0.05))
        {
            createSection (createRange, true);
        }
        else if (drag == Drag::move && dragSection != nullptr)
        {
            if (e.mouseWasDraggedSinceMouseDown())
            {
                auto range = dragSection->getEditTimeRange();
                auto dest = findDropTime (e.x);

                if (dragCopy)
                {
                    um.beginNewTransaction ("Copy section");
                    ArrangerOps::copyRange (edit, range, dest);
                }
                else if (dest < range.getStart() || dest > range.getEnd())
                {
                    um.beginNewTransaction ("Move section");
                    ArrangerOps::moveSection (edit, *dragSection, dest);
                }
            }
            else
            {
                // Click: jump there; Shift-click also loops the section.
                transport.setPosition (dragSection->getPosition().getStart());

                if (e.mods.isShiftDown())
                {
                    transport.setLoopRange (dragSection->getEditTimeRange());
                    transport.looping = true;
                }
            }
        }

        drag = Drag::none;
        dragSection = nullptr;
        createRange = {};
        dragX = dropX = -1;
        repaint();
    }

    void ArrangerLane::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.x < headerWidth)
            return;

        if (auto s = sectionAt (e.x))
            startRename (*s);
        else if (auto r = rangeForNewSection (xToTime (e.x)); ! r.isEmpty())
            createSection (r, true);
    }

    //==============================================================================
    void ArrangerLane::createSection (te::TimeRange range, bool rename)
    {
        edit.getUndoManager().beginNewTransaction ("Add section");
        te::ArrangerClip::Ptr s = ArrangerOps::addSection (edit, range, suggestName (range.getStart()));

        if (s != nullptr)
        {
            selectedSection = s;

            if (rename)
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ArrangerLane> (this), s]
                {
                    if (safe != nullptr && s->getClipTrack() != nullptr)
                        safe->startRename (*s);
                });
        }
    }

    void ArrangerLane::addSectionAtPlayhead()
    {
        auto& transport = edit.getTransport();
        auto loop = transport.getLoopRange();

        auto range = ! loop.isEmpty() && transport.looping.get() ? loop
                   : viewState.hasRange()                         ? viewState.getRange()
                                                                  : rangeForNewSection (transport.getPosition());

        if (range.isEmpty())
        {
            edit.engine.getUIBehaviour().showWarningMessage ("There's already a section at the playhead. "
                                                             "Move the playhead to an empty spot, or set a loop range.");
            return;
        }

        createSection (range, true);
    }

    void ArrangerLane::startRename (te::ArrangerClip& section)
    {
        te::ArrangerClip::Ptr s = &section;
        nameEditor = std::make_unique<juce::TextEditor>();
        auto b = getSectionBounds (section).withTrimmedLeft (juce::jmax (0, headerWidth - getSectionBounds (section).getX()));
        nameEditor->setBounds (b.withWidth (juce::jmax (110, b.getWidth())).reduced (1));
        nameEditor->setFont (juce::FontOptions (12.5f, juce::Font::bold));
        nameEditor->setText (section.getName(), false);
        nameEditor->selectAll();
        nameEditor->setColour (juce::TextEditor::backgroundColourId, Palette::panelRaised);
        nameEditor->setColour (juce::TextEditor::textColourId, Palette::text);

        auto commit = [this, s]
        {
            if (nameEditor == nullptr)
                return;

            auto text = nameEditor->getText().trim();

            if (text.isNotEmpty() && text != s->getName())
            {
                edit.getUndoManager().beginNewTransaction ("Rename section");
                ArrangerOps::renameSection (*s, text);
            }

            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ArrangerLane> (this)]
                                             { if (safe != nullptr) safe->nameEditor.reset(); });
        };

        nameEditor->onReturnKey = commit;
        nameEditor->onFocusLost = commit;
        nameEditor->onEscapeKey = [this]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ArrangerLane> (this)]
                                             { if (safe != nullptr) safe->nameEditor.reset(); });
        };

        addAndMakeVisible (*nameEditor);
        nameEditor->grabKeyboardFocus();
    }

    //==============================================================================
    void ArrangerLane::showSectionMenu (te::ArrangerClip& section)
    {
        te::ArrangerClip::Ptr s = &section;
        auto* um = &edit.getUndoManager();
        auto sections = ArrangerOps::getSections (edit);
        auto index = sections.indexOf (&section);
        te::ArrangerClip::Ptr previous = index > 0 ? sections[index - 1] : nullptr;
        te::ArrangerClip::Ptr next = index + 1 < sections.size() ? sections[index + 1] : nullptr;

        juce::PopupMenu m;
        m.addSectionHeader (section.getName());
        m.addItem ("Rename...", [this, s] { startRename (*s); });

        juce::PopupMenu names;
        for (auto& name : ArrangerOps::getPresetNames())
        {
            juce::PopupMenu::Item item (name);
            item.setColour (ArrangerOps::getColourForName (name));
            item.setTicked (section.getName() == name);
            item.setAction ([s, name, um] { um->beginNewTransaction ("Rename section"); ArrangerOps::renameSection (*s, name); });
            names.addItem (item);
        }
        m.addSubMenu ("Name", names);

        juce::PopupMenu colours;
        for (int i = 0; i < Palette::trackColours.size(); ++i)
        {
            auto c = Palette::trackColours[i];
            juce::PopupMenu::Item item ("Colour " + juce::String (i + 1));
            item.setColour (c);
            item.setAction ([s, c, um] { um->beginNewTransaction ("Section colour"); s->setColour (c); });
            colours.addItem (item);
        }
        m.addSubMenu ("Colour", colours);

        m.addSeparator();
        m.addItem ("Duplicate Section", [this, s, um]
        {
            um->beginNewTransaction ("Duplicate section");
            ArrangerOps::duplicateSection (edit, *s);
        });
        m.addItem ("Move Earlier (before " + (previous != nullptr ? previous->getName() : juce::String ("previous")) + ")",
                   previous != nullptr, false, [this, s, previous, um]
        {
            um->beginNewTransaction ("Move section");
            ArrangerOps::moveSection (edit, *s, previous->getPosition().getStart());
        });
        m.addItem ("Move Later (after " + (next != nullptr ? next->getName() : juce::String ("next")) + ")",
                   next != nullptr, false, [this, s, next, um]
        {
            um->beginNewTransaction ("Move section");
            ArrangerOps::moveSection (edit, *s, next->getPosition().getEnd());
        });

        m.addSeparator();
        m.addItem ("Play from Here", [this, s] { edit.getTransport().setPosition (s->getPosition().getStart()); });
        m.addItem ("Loop This Section", [this, s]
        {
            auto& transport = edit.getTransport();
            transport.setLoopRange (s->getEditTimeRange());
            transport.looping = true;
        });
        m.addItem ("Select Its Time Range", [this, s] { viewState.setRange (s->getEditTimeRange()); });

        m.addSeparator();
        m.addItem ("Delete Section and Its Content", [this, s, um]
        {
            um->beginNewTransaction ("Delete section");
            selectedSection = nullptr;
            ArrangerOps::deleteSection (edit, *s, &viewState.selectionManager);
        });
        m.addItem ("Remove Section Marker Only", [this, s, um]
        {
            um->beginNewTransaction ("Remove section marker");
            selectedSection = nullptr;
            s->removeFromParent();
        });

        m.showMenuAsync ({});
    }

    void ArrangerLane::showEmptyMenu (te::TimePosition t)
    {
        auto range = rangeForNewSection (t);
        auto loop = edit.getTransport().getLoopRange();
        juce::PopupMenu m;

        m.addItem ("Add Section Here", ! range.isEmpty(), false, [this, range] { createSection (range, true); });
        m.addItem ("Add Section over Loop Range", ! loop.isEmpty(), false, [this, loop] { createSection (loop, true); });
        m.addItem ("Add Section over Selected Range", viewState.hasRange(), false, [this] { createSection (viewState.getRange(), true); });
        m.showMenuAsync ({});
    }
}
