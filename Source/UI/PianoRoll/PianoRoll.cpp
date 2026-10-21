#include "PianoRoll.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    static bool isBlackKey (int note)
    {
        const int n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    //==============================================================================
    struct BeatRuler : public juce::Component
    {
        explicit BeatRuler (PianoRoll& o) : owner (o) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            auto& ts = owner.edit.tempoSequence;
            auto firstBeat = std::floor (owner.layout.scrollBeat);
            auto lastBeat = owner.xToEditBeat (getWidth());
            g.setFont (juce::FontOptions (11.0f));

            for (double b = juce::jmax (0.0, firstBeat); b <= lastBeat; b += 1.0)
            {
                auto x = (int) owner.editBeatToX (b);
                auto t = ts.toTime (te::BeatPosition::fromBeats (b));
                auto bb = ts.toBarsAndBeats (t);
                const bool isBar = bb.beats.inBeats() < 0.001;

                g.setColour (isBar ? Palette::textDim : Palette::gridBar);
                g.drawVerticalLine (x, isBar ? 4.0f : (float) getHeight() * 0.6f, (float) getHeight());

                if (isBar || owner.layout.pixelsPerBeat > 50.0)
                {
                    g.setColour (isBar ? Palette::text : Palette::textDim);
                    auto label = juce::String (bb.bars + 1) + (isBar ? juce::String() : "." + juce::String (bb.getWholeBeats() + 1));
                    g.drawText (label, x + 3, 2, 50, getHeight() - 4, juce::Justification::centredLeft, false);
                }
            }

            // Clip extent
            auto clipStart = owner.edit.tempoSequence.toBeats (owner.clip->getPosition().getStart()).inBeats();
            auto clipEnd = owner.edit.tempoSequence.toBeats (owner.clip->getPosition().getEnd()).inBeats();
            g.setColour (owner.clip->getColour().withAlpha (0.6f));
            g.fillRect ((float) owner.editBeatToX (clipStart), (float) getHeight() - 3.0f,
                        (float) ((clipEnd - clipStart) * owner.layout.pixelsPerBeat), 3.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto beat = owner.snapEditBeat (owner.xToEditBeat (e.x));
            owner.edit.getTransport().setPosition (owner.edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)));
        }

        PianoRoll& owner;
    };

    //==============================================================================
    struct KeysPanel : public juce::Component
    {
        explicit KeysPanel (PianoRoll& o) : owner (o) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            const float h = owner.layout.rowHeight;
            g.setFont (juce::FontOptions (juce::jlimit (9.0f, 13.0f, h - 1.0f)));

            for (int row = 0; row < owner.getNumRows(); ++row)
            {
                auto y = owner.rowToY (row);
                if (y + h < 0 || y > getHeight())
                    continue;

                auto note = owner.rowToNote (row);
                auto r = juce::Rectangle<float> (0.0f, y, (float) getWidth(), h);

                if (owner.isDrumMode())
                {
                    g.setColour (row % 2 == 0 ? Palette::panelRaised : Palette::panelBackground);
                    g.fillRect (r);
                    g.setColour (Palette::text);
                    auto name = owner.drumMap->getNameForNote (note);
                    g.drawText (juce::String (note) + "  " + (name.isNotEmpty() ? name : juce::MidiMessage::getMidiNoteName (note, true, true, 3)),
                                r.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
                }
                else
                {
                    const bool black = isBlackKey (note);
                    g.setColour (black ? juce::Colour (0xff15171a) : juce::Colour (0xffd8dbe0));
                    g.fillRect (black ? r.withTrimmedRight ((float) getWidth() * 0.35f) : r);

                    if (note % 12 == 0 && h >= 8.0f)
                    {
                        g.setColour (juce::Colours::black);
                        g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3), r.reduced (4.0f, 0.0f),
                                    juce::Justification::centredRight, false);
                    }
                }

                g.setColour (Palette::windowBackground.withAlpha (0.6f));
                g.drawHorizontalLine ((int) (y + h), 0.0f, (float) getWidth());
            }

            g.setColour (Palette::panelOutline);
            g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto row = owner.yToRow ((float) e.y);
            if (juce::isPositiveAndBelow (row, owner.getNumRows()))
                owner.audition (owner.rowToNote (row));
        }

        PianoRoll& owner;
    };

    //==============================================================================
    struct NoteGrid : public juce::Component
    {
        explicit NoteGrid (PianoRoll& o) : owner (o)
        {
            setWantsKeyboardFocus (false);
        }

        void paint (juce::Graphics& g) override
        {
            auto& L = owner.layout;
            g.fillAll (Palette::laneBackground);

            // Row backgrounds
            for (int row = 0; row < owner.getNumRows(); ++row)
            {
                auto y = owner.rowToY (row);
                if (y + L.rowHeight < 0 || y > getHeight())
                    continue;

                auto note = owner.rowToNote (row);
                const bool shade = owner.isDrumMode() ? (row % 2 == 1) : isBlackKey (note);

                if (shade)
                {
                    g.setColour (Palette::laneAlternate);
                    g.fillRect (0.0f, y, (float) getWidth(), L.rowHeight);
                }

                if (! owner.isDrumMode() && note % 12 == 0)
                {
                    g.setColour (Palette::gridBar);
                    g.drawHorizontalLine ((int) (y + L.rowHeight), 0.0f, (float) getWidth());
                }
            }

            // Vertical grid
            auto& ts = owner.edit.tempoSequence;
            auto gridBeats = owner.getGridBeats();
            auto first = std::floor (L.scrollBeat / gridBeats) * gridBeats;
            auto last = owner.xToEditBeat (getWidth());

            if (gridBeats * L.pixelsPerBeat > 5.0)
            {
                for (double b = juce::jmax (0.0, first); b <= last; b += gridBeats)
                {
                    auto x = (int) owner.editBeatToX (b);
                    auto bb = ts.toBarsAndBeats (ts.toTime (te::BeatPosition::fromBeats (b)));
                    const bool isBar = bb.beats.inBeats() < 0.001;
                    const bool isBeat = std::abs (b - std::round (b)) < 1e-6;
                    g.setColour (isBar ? Palette::gridBar.brighter (0.15f) : isBeat ? Palette::gridBar : Palette::gridSub.brighter (0.05f));
                    g.drawVerticalLine (x, 0.0f, (float) getHeight());
                }
            }

            // Outside the clip: darker
            auto clipStart = ts.toBeats (owner.clip->getPosition().getStart()).inBeats();
            auto clipEnd = ts.toBeats (owner.clip->getPosition().getEnd()).inBeats();
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (0.0f, 0.0f, (float) owner.editBeatToX (clipStart), (float) getHeight());
            g.fillRect ((float) owner.editBeatToX (clipEnd), 0.0f, (float) getWidth(), (float) getHeight());

            // Notes
            auto colour = owner.clip->getColour();

            for (auto n : owner.clip->getSequence().getNotes())
            {
                auto r = owner.getNoteBounds (*n);
                if (r.getRight() < 0 || r.getX() > getWidth() || r.getBottom() < 0 || r.getY() > getHeight())
                    continue;

                const bool sel = owner.selected.contains (n);
                auto c = colour.withMultipliedBrightness (0.55f + 0.45f * n->getVelocity() / 127.0f);
                if (n->isMute())
                    c = c.withSaturation (0.0f).withAlpha (0.5f);

                if (owner.isDrumMode())
                {
                    juce::Path diamond;
                    auto cx = r.getX() + juce::jmin (r.getHeight() * 0.5f, r.getWidth() * 0.5f);
                    auto rad = r.getHeight() * 0.45f;
                    diamond.addTriangle (cx - rad, r.getCentreY(), cx, r.getCentreY() - rad, cx + rad, r.getCentreY());
                    diamond.addTriangle (cx - rad, r.getCentreY(), cx, r.getCentreY() + rad, cx + rad, r.getCentreY());
                    g.setColour (c.withAlpha (0.35f));
                    g.fillRect (r.reduced (0.0f, r.getHeight() * 0.35f));
                    g.setColour (c);
                    g.fillPath (diamond);
                    g.setColour (sel ? juce::Colours::white : c.darker (0.6f));
                    g.strokePath (diamond, juce::PathStrokeType (sel ? 2.0f : 1.0f));
                }
                else
                {
                    g.setColour (c);
                    g.fillRoundedRectangle (r, 2.0f);
                    g.setColour (sel ? juce::Colours::white : c.darker (0.6f));
                    g.drawRoundedRectangle (r, 2.0f, sel ? 2.0f : 1.0f);
                }
            }

            // Rubber band
            if (! band.isEmpty())
            {
                g.setColour (Palette::selection);
                g.fillRect (band);
                g.setColour (Palette::accent);
                g.drawRect (band);
            }

            // Playhead
            auto playBeat = ts.toBeats (owner.edit.getTransport().getPosition()).inBeats();
            g.setColour (Palette::playhead);
            g.drawVerticalLine ((int) owner.editBeatToX (playBeat), 0.0f, (float) getHeight());
        }

        enum class Mode { none, move, resize, band, draw };

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.grabKeyboardFocus();
            auto& um = owner.edit.getUndoManager();
            auto pos = e.position;
            auto* note = owner.noteAt (pos);
            auto tool = owner.viewState.getTool();
            mode = Mode::none;

            if (e.mods.isPopupMenu())
            {
                showMenu (note);
                return;
            }

            if (tool == Tool::erase || (note != nullptr && e.mods.isAltDown() && e.mods.isShiftDown()))
            {
                if (note != nullptr)
                {
                    um.beginNewTransaction ("Delete note");
                    owner.selected.removeAllInstancesOf (note);
                    owner.clip->getSequence().removeNote (*note, &um);
                }
                return;
            }

            if (note != nullptr)
            {
                if (e.mods.isCommandDown())
                {
                    if (owner.selected.contains (note)) owner.selected.removeAllInstancesOf (note);
                    else                                owner.selected.addIfNotAlreadyThere (note);
                }
                else if (e.mods.isShiftDown())
                {
                    owner.selected.addIfNotAlreadyThere (note);
                }
                else if (! owner.selected.contains (note))
                {
                    owner.selected = { note };
                }

                if (! owner.selected.contains (note))
                {
                    repaint();
                    return;
                }

                um.beginNewTransaction ("Edit notes");

                // Alt-drag copies: duplicate the selection in place and drag the copies.
                if (e.mods.isAltDown())
                {
                    juce::Array<te::MidiNote*> copies;
                    te::MidiNote* grabbedCopy = nullptr;

                    for (auto n : owner.selected)
                    {
                        auto* c = owner.clip->getSequence().addNote (n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(),
                                                                     n->getVelocity(), n->getColour(), &um);
                        copies.add (c);
                        if (n == note) grabbedCopy = c;
                    }

                    owner.selected = copies;
                    note = grabbedCopy != nullptr ? grabbedCopy : note;
                }

                grabbed = note;
                auto r = owner.getNoteBounds (*note);
                mode = (pos.x > r.getRight() - 6.0f && r.getWidth() > 10.0f && ! owner.isDrumMode()) ? Mode::resize : Mode::move;
                captureOriginals();
                startRow = owner.yToRow (pos.y);
                owner.lastVelocity = note->getVelocity();
                owner.audition (note->getNoteNumber(), note->getVelocity());
                repaint();
                return;
            }

            // Empty space
            if (tool == Tool::draw || e.getNumberOfClicks() >= 2)
            {
                auto row = owner.yToRow (pos.y);
                if (! juce::isPositiveAndBelow (row, owner.getNumRows()))
                    return;

                auto gridBeats = owner.getGridBeats();
                auto startBeat = std::floor (owner.xToEditBeat (pos.x) / gridBeats) * gridBeats;
                auto pitch = owner.rowToNote (row);

                um.beginNewTransaction ("Add note");
                auto contentStart = owner.editBeatToContentBeat (startBeat);
                auto contentEnd = owner.editBeatToContentBeat (startBeat + gridBeats);
                auto* n = owner.clip->getSequence().addNote (pitch, contentStart, contentEnd - contentStart,
                                                             owner.lastVelocity, 0, &um);
                owner.selected = { n };
                grabbed = n;
                mode = owner.isDrumMode() ? Mode::move : Mode::resize;
                captureOriginals();
                startRow = row;
                owner.audition (pitch, owner.lastVelocity);
                repaint();
                return;
            }

            if (! e.mods.isShiftDown() && ! e.mods.isCommandDown())
                owner.selected.clear();

            mode = Mode::band;
            bandStart = pos;
            repaint();
        }

        void captureOriginals()
        {
            originals.clear();

            for (auto n : owner.selected)
                originals.push_back ({ n, owner.contentBeatToEditBeat (n->getStartBeat()),
                                       owner.contentBeatToEditBeat (n->getEndBeat()), n->getNoteNumber() });
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto& um = owner.edit.getUndoManager();
            auto pos = e.position;

            if (mode == Mode::band)
            {
                band = juce::Rectangle<float> (bandStart, pos);
                owner.selected.clear();

                for (auto n : owner.clip->getSequence().getNotes())
                    if (band.intersects (owner.getNoteBounds (*n)))
                        owner.selected.add (n);

                repaint();
                return;
            }

            if (grabbed == nullptr || originals.empty())
                return;

            const Original* grabbedOrig = nullptr;
            for (auto& o : originals)
                if (o.note == grabbed)
                    grabbedOrig = &o;

            if (grabbedOrig == nullptr)
                return;

            if (mode == Mode::move)
            {
                auto rawStart = grabbedOrig->editStart + e.getDistanceFromDragStartX() / owner.layout.pixelsPerBeat;
                auto newStart = e.mods.isShiftDown() ? rawStart : owner.snapEditBeat (rawStart);
                auto deltaBeats = newStart - grabbedOrig->editStart;
                auto deltaRows = owner.yToRow (pos.y) - startRow;

                for (auto& o : originals)
                {
                    auto start = juce::jmax (0.0, o.editStart + deltaBeats);
                    auto len = o.editEnd - o.editStart;
                    auto cs = owner.editBeatToContentBeat (start);
                    auto ce = owner.editBeatToContentBeat (start + len);
                    o.note->setStartAndLength (cs, ce - cs, &um);

                    int newPitch = o.pitch;
                    if (owner.isDrumMode())
                    {
                        auto row = owner.noteToRow (o.pitch);
                        if (row >= 0)
                            newPitch = owner.rowToNote (juce::jlimit (0, owner.getNumRows() - 1, row + deltaRows));
                    }
                    else
                    {
                        newPitch = juce::jlimit (0, 127, o.pitch - deltaRows);
                    }

                    if (newPitch != o.note->getNoteNumber())
                    {
                        o.note->setNoteNumber (newPitch, &um);
                        if (o.note == grabbed)
                            owner.audition (newPitch, o.note->getVelocity());
                    }
                }
            }
            else if (mode == Mode::resize)
            {
                auto rawEnd = owner.xToEditBeat (pos.x);
                auto newEnd = e.mods.isShiftDown() ? rawEnd : owner.snapEditBeat (rawEnd);
                auto deltaBeats = newEnd - grabbedOrig->editEnd;
                auto minLen = juce::jmin (owner.getGridBeats(), 0.0625);

                for (auto& o : originals)
                {
                    auto end = juce::jmax (o.editStart + minLen, o.editEnd + deltaBeats);
                    auto cs = owner.editBeatToContentBeat (o.editStart);
                    auto ce = owner.editBeatToContentBeat (end);
                    o.note->setStartAndLength (cs, ce - cs, &um);
                }
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            mode = Mode::none;
            band = {};
            grabbed = nullptr;
            originals.clear();

            if (auto at = dynamic_cast<te::AudioTrack*> (owner.clip->getTrack()))
                at->turnOffGuideNotes();

            repaint();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            auto* note = owner.noteAt (e.position);
            bool edge = note != nullptr && ! owner.isDrumMode() && e.position.x > owner.getNoteBounds (*note).getRight() - 6.0f;
            setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                                 : owner.viewState.getTool() == Tool::draw ? juce::MouseCursor::CrosshairCursor
                                                                           : juce::MouseCursor::NormalCursor);
        }

        void showMenu (te::MidiNote* note)
        {
            if (note != nullptr && ! owner.selected.contains (note))
                owner.selected = { note };

            juce::PopupMenu m;
            m.addItem ("Quantize " + juce::String (owner.selected.isEmpty() ? "all notes" : "selected notes"), [this] { owner.quantizeNotes (! owner.selected.isEmpty()); });
            m.addItem ("Duplicate", ! owner.selected.isEmpty(), false, [this] { owner.duplicateSelected(); });
            m.addItem ("Select All", [this] { owner.selectAll(); });
            m.addSeparator();

            juce::PopupMenu vel;
            for (int v : { 127, 110, 100, 80, 64, 40 })
                vel.addItem (juce::String (v), ! owner.selected.isEmpty(), false, [this, v]
                {
                    auto& um = owner.edit.getUndoManager();
                    um.beginNewTransaction ("Set velocity");
                    for (auto n : owner.selected)
                        n->setVelocity (v, &um);
                    owner.lastVelocity = v;
                });
            m.addSubMenu ("Set Velocity", vel);
            m.addSeparator();
            m.addItem ("Delete", ! owner.selected.isEmpty(), false, [this] { owner.deleteSelected(); });
            m.showMenuAsync ({});
        }

        struct Original
        {
            te::MidiNote* note;
            double editStart, editEnd;
            int pitch;
        };

        PianoRoll& owner;
        Mode mode = Mode::none;
        te::MidiNote* grabbed = nullptr;
        std::vector<Original> originals;
        juce::Point<float> bandStart;
        juce::Rectangle<float> band;
        int startRow = 0;
    };

    //==============================================================================
    struct VelocityLane : public juce::Component
    {
        explicit VelocityLane (PianoRoll& o) : owner (o) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            g.setColour (Palette::panelOutline);
            g.drawHorizontalLine (0, 0.0f, (float) getWidth());

            auto h = (float) getHeight() - 4.0f;

            for (auto n : owner.clip->getSequence().getNotes())
            {
                auto x = (float) owner.editBeatToX (owner.contentBeatToEditBeat (n->getStartBeat()));
                if (x < -4 || x > getWidth())
                    continue;

                auto vh = h * n->getVelocity() / 127.0f;
                const bool sel = owner.selected.contains (n);
                g.setColour (sel ? Palette::accent : owner.clip->getColour().withAlpha (0.8f));
                g.fillRect (x, getHeight() - vh, 3.0f, vh);
                g.fillEllipse (x - 2.0f, getHeight() - vh - 3.0f, 7.0f, 7.0f);
            }

            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("Velocity", 4, 2, 60, 12, juce::Justification::left);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.edit.getUndoManager().beginNewTransaction ("Change velocity");
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto& um = owner.edit.getUndoManager();
            auto vel = juce::jlimit (1, 127, juce::roundToInt ((1.0f - (float) e.y / (float) getHeight()) * 127.0f));
            const bool onlySelected = ! owner.selected.isEmpty();

            for (auto n : owner.clip->getSequence().getNotes())
            {
                auto x = owner.editBeatToX (owner.contentBeatToEditBeat (n->getStartBeat()));

                if (std::abs (x - e.x) <= 4.0 && (! onlySelected || owner.selected.contains (n)))
                {
                    n->setVelocity (vel, &um);
                    owner.lastVelocity = vel;
                }
            }

            repaint();
        }

        PianoRoll& owner;
    };

    //==============================================================================
    PianoRoll::PianoRoll (EditViewState& vs, te::MidiClip::Ptr c)
        : viewState (vs), edit (vs.edit), clip (c)
    {
        setWantsKeyboardFocus (true);

        ruler = std::make_unique<BeatRuler> (*this);
        keys = std::make_unique<KeysPanel> (*this);
        grid = std::make_unique<NoteGrid> (*this);
        velocity = std::make_unique<VelocityLane> (*this);

        for (auto* comp : { ruler.get(), keys.get(), grid.get(), velocity.get() })
            addAndMakeVisible (comp);

        titleLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (titleLabel);

        quantizeButton.setTooltip ("Quantize selected notes (or all) to the grid (Q)");
        quantizeButton.onClick = [this] { quantizeNotes (! selected.isEmpty()); };
        addAndMakeVisible (quantizeButton);

        drumModeButton.setTooltip ("Drum map for this track (rows = drum pieces)");
        drumModeButton.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem ("Piano roll (no drum map)", true, ! isDrumMode(), [this]
            {
                if (auto t = clip->getTrack()) DrumMapLibrary::setForTrack (*t, {});
                setDrumMap ({});
            });

            for (auto& map : DrumMapLibrary::getAll())
                m.addItem (map.name, true, isDrumMode() && drumMap->name == map.name, [this, map]
                {
                    if (auto t = clip->getTrack()) DrumMapLibrary::setForTrack (*t, map.name);
                    setDrumMap (map);
                });

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drumModeButton));
        };
        addAndMakeVisible (drumModeButton);

        vScroll.setAutoHide (false);
        vScroll.addListener (this);
        addAndMakeVisible (vScroll);

        clip->state.addListener (this);

        auto* track = clip->getTrack();
        setDrumMap (track != nullptr ? DrumMapLibrary::getForTrack (*track) : std::nullopt);

        // Show the clip from its start.
        layout.scrollBeat = juce::jmax (0.0, edit.tempoSequence.toBeats (clip->getPosition().getStart()).inBeats() - 0.5);
        startTimerHz (30);
    }

    PianoRoll::~PianoRoll()
    {
        clip->state.removeListener (this);
    }

    void PianoRoll::setDrumMap (std::optional<DrumMap> map)
    {
        drumMap = std::move (map);
        layout.rowHeight = isDrumMode() ? 20.0f : 12.0f;
        rebuildRows();
        centreOnNotes();
        drumModeButton.setButtonText (isDrumMode() ? "Drum map: " + drumMap->name : juce::String ("Piano roll"));
        resized();
        repaint();
    }

    void PianoRoll::rebuildRows()
    {
        rowNotes.clear();

        if (isDrumMode())
        {
            for (auto& [note, name] : drumMap->pieces)
                if (std::find (rowNotes.begin(), rowNotes.end(), note) == rowNotes.end())
                    rowNotes.push_back (note);

            // Notes in the clip that the map doesn't name get their own rows at the end.
            for (auto n : clip->getSequence().getNotes())
                if (std::find (rowNotes.begin(), rowNotes.end(), n->getNoteNumber()) == rowNotes.end())
                    rowNotes.push_back (n->getNoteNumber());
        }
        else
        {
            for (int n = 127; n >= 0; --n)
                rowNotes.push_back (n);
        }
    }

    int PianoRoll::noteToRow (int note) const
    {
        auto it = std::find (rowNotes.begin(), rowNotes.end(), note);
        return it == rowNotes.end() ? -1 : (int) std::distance (rowNotes.begin(), it);
    }

    int PianoRoll::rowToNote (int row) const
    {
        return juce::isPositiveAndBelow (row, (int) rowNotes.size()) ? rowNotes[(size_t) row] : 60;
    }

    double PianoRoll::contentBeatToEditBeat (te::BeatPosition b) const
    {
        return edit.tempoSequence.toBeats (clip->getTimeOfContentBeat (b)).inBeats();
    }

    te::BeatPosition PianoRoll::editBeatToContentBeat (double editBeat) const
    {
        return clip->getContentBeatAtTime (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (editBeat)));
    }

    double PianoRoll::getGridBeats() const
    {
        auto grid = viewState.getGrid();
        auto t = edit.tempoSequence.toTime (te::BeatPosition::fromBeats (layout.scrollBeat));
        auto& sig = edit.tempoSequence.getTimeSigAt (t);
        auto den = juce::jmax (1, sig.denominator.get());

        if (grid.wholeNotes <= 0.0)
            return (double) juce::jmax (1, sig.numerator.get());

        return grid.wholeNotes * den;
    }

    double PianoRoll::snapEditBeat (double editBeat) const
    {
        if (! viewState.snapEnabled.get())
            return editBeat;

        auto g = getGridBeats();
        return std::round (editBeat / g) * g;
    }

    juce::Rectangle<float> PianoRoll::getNoteBounds (const te::MidiNote& n) const
    {
        auto row = noteToRow (n.getNoteNumber());
        auto x1 = (float) editBeatToX (contentBeatToEditBeat (n.getStartBeat()));
        auto x2 = (float) editBeatToX (contentBeatToEditBeat (n.getEndBeat()));
        return { x1, rowToY (row) + 1.0f, juce::jmax (3.0f, x2 - x1), layout.rowHeight - 2.0f };
    }

    te::MidiNote* PianoRoll::noteAt (juce::Point<float> p) const
    {
        auto& notes = clip->getSequence().getNotes();

        for (int i = notes.size(); --i >= 0;)
        {
            auto r = getNoteBounds (*notes[i]);
            if (isDrumMode())
                r = r.withWidth (juce::jmax (r.getWidth(), r.getHeight()));

            if (r.contains (p))
                return notes[i];
        }

        return nullptr;
    }

    void PianoRoll::audition (int note, int vel)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
            at->playGuideNote (note, clip->getMidiChannel(), vel, true, true, true);
    }

    void PianoRoll::validateSelection()
    {
        auto& notes = clip->getSequence().getNotes();
        selected.removeIf ([&notes] (te::MidiNote* n) { return ! notes.contains (n); });
    }

    void PianoRoll::changed()
    {
        validateSelection();

        if (isDrumMode())
        {
            auto before = rowNotes.size();
            rebuildRows();
            if (rowNotes.size() != before)
                updateScrollBar();
        }

        repaint();
    }

    void PianoRoll::timerCallback()
    {
        auto beat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();

        if (std::abs (beat - lastPlayheadBeat) > 1e-4)
        {
            // Follow playback.
            if (edit.getTransport().isPlaying() && viewState.followPlayhead.get())
            {
                auto x = editBeatToX (beat);
                if (x > grid->getWidth() * 0.92 || x < 0)
                    layout.scrollBeat = juce::jmax (0.0, beat - 1.0);
            }

            lastPlayheadBeat = beat;
            grid->repaint();
            ruler->repaint();
        }
    }

    void PianoRoll::updateScrollBar()
    {
        auto total = getNumRows() * layout.rowHeight;
        layout.scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, total - (float) grid->getHeight()), layout.scrollY);
        vScroll.setRangeLimits (0.0, juce::jmax ((double) total, (double) grid->getHeight()));
        vScroll.setCurrentRange (layout.scrollY, grid->getHeight(), juce::dontSendNotification);
    }

    void PianoRoll::centreOnNotes()
    {
        auto& notes = clip->getSequence().getNotes();

        if (isDrumMode())
        {
            layout.scrollY = 0.0f;
            return;
        }

        int lo = 60, hi = 72;
        if (! notes.isEmpty())
        {
            lo = 127; hi = 0;
            for (auto n : notes) { lo = juce::jmin (lo, n->getNoteNumber()); hi = juce::jmax (hi, n->getNoteNumber()); }
        }

        auto centreRow = noteToRow ((lo + hi) / 2);
        layout.scrollY = juce::jmax (0.0f, centreRow * layout.rowHeight - (float) juce::jmax (100, grid != nullptr ? grid->getHeight() : 300) * 0.5f);
    }

    void PianoRoll::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
    }

    void PianoRoll::resized()
    {
        auto r = getLocalBounds();
        auto toolbar = r.removeFromTop (28).reduced (6, 3);
        titleLabel.setText (clip->getName() + "  -  " + (clip->getTrack() != nullptr ? clip->getTrack()->getName() : juce::String()),
                            juce::dontSendNotification);
        drumModeButton.setBounds (toolbar.removeFromRight (180));
        toolbar.removeFromRight (6);
        quantizeButton.setBounds (toolbar.removeFromRight (80));
        titleLabel.setBounds (toolbar);

        const int keysWidth = isDrumMode() ? 170 : 70;
        auto velArea = r.removeFromBottom (70);
        auto top = r.removeFromTop (20);

        vScroll.setBounds (r.removeFromRight (12));
        keys->setBounds (r.removeFromLeft (keysWidth));
        grid->setBounds (r);
        ruler->setBounds (top.withTrimmedLeft (keysWidth).withTrimmedRight (12));
        velocity->setBounds (velArea.withTrimmedLeft (keysWidth).withTrimmedRight (12));

        updateScrollBar();
    }

    void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        auto local = e.getEventRelativeTo (grid.get());

        if (e.mods.isCommandDown())
        {
            auto anchorBeat = xToEditBeat (local.x);
            layout.pixelsPerBeat = juce::jlimit (4.0, 800.0, layout.pixelsPerBeat * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            layout.scrollBeat = juce::jmax (0.0, anchorBeat - local.x / layout.pixelsPerBeat);
        }
        else if (e.mods.isAltDown())
        {
            layout.rowHeight = juce::jlimit (6.0f, 40.0f, layout.rowHeight * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f));
            updateScrollBar();
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            auto d = std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY;
            layout.scrollBeat = juce::jmax (0.0, layout.scrollBeat - d * 300.0 / layout.pixelsPerBeat);
        }
        else
        {
            layout.scrollY -= w.deltaY * 300.0f;
            updateScrollBar();
        }

        repaint();
    }

    bool PianoRoll::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)  { deleteSelected(); return true; }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))     { selectAll(); return true; }
        if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))     { duplicateSelected(); return true; }
        if (k.getKeyCode() == 'Q' && ! k.getModifiers().isAnyModifierKeyDown())     { quantizeNotes (! selected.isEmpty()); return true; }
        if (k.getKeyCode() == juce::KeyPress::upKey)   { transposeSelected (k.getModifiers().isShiftDown() ? 12 : 1); return true; }
        if (k.getKeyCode() == juce::KeyPress::downKey) { transposeSelected (k.getModifiers().isShiftDown() ? -12 : -1); return true; }
        if (k == juce::KeyPress::escapeKey)                                         { selected.clear(); repaint(); return true; }
        return false;
    }

    //==============================================================================
    void PianoRoll::quantizeNotes (bool selectedOnly)
    {
        auto& um = edit.getUndoManager();
        um.beginNewTransaction ("Quantize");
        auto g = getGridBeats();

        juce::Array<te::MidiNote*> notes = selectedOnly ? selected : clip->getSequence().getNotes();

        for (auto n : notes)
        {
            auto start = contentBeatToEditBeat (n->getStartBeat());
            auto end = contentBeatToEditBeat (n->getEndBeat());
            auto snapped = std::round (start / g) * g;
            auto cs = editBeatToContentBeat (snapped);
            auto ce = editBeatToContentBeat (snapped + (end - start));
            n->setStartAndLength (cs, ce - cs, &um);
        }

        repaint();
    }

    void PianoRoll::deleteSelected()
    {
        auto& um = edit.getUndoManager();
        um.beginNewTransaction ("Delete notes");
        auto toDelete = selected;
        selected.clear();

        for (auto n : toDelete)
            clip->getSequence().removeNote (*n, &um);
    }

    void PianoRoll::selectAll()
    {
        selected = clip->getSequence().getNotes();
        repaint();
    }

    void PianoRoll::transposeSelected (int semitones)
    {
        auto& um = edit.getUndoManager();
        um.beginNewTransaction ("Transpose");

        for (auto n : selected)
        {
            if (isDrumMode())
            {
                auto row = noteToRow (n->getNoteNumber()) - (semitones > 0 ? 1 : -1);
                if (juce::isPositiveAndBelow (row, getNumRows()))
                    n->setNoteNumber (rowToNote (row), &um);
            }
            else
            {
                n->setNoteNumber (juce::jlimit (0, 127, n->getNoteNumber() + semitones), &um);
            }
        }

        repaint();
    }

    void PianoRoll::duplicateSelected()
    {
        if (selected.isEmpty())
            return;

        auto& um = edit.getUndoManager();
        um.beginNewTransaction ("Duplicate notes");

        double first = 1e9, last = -1e9;
        for (auto n : selected)
        {
            first = juce::jmin (first, contentBeatToEditBeat (n->getStartBeat()));
            last = juce::jmax (last, contentBeatToEditBeat (n->getEndBeat()));
        }

        auto g = getGridBeats();
        auto offset = std::ceil ((last - first) / g - 1e-6) * g;
        juce::Array<te::MidiNote*> copies;

        for (auto n : selected)
        {
            auto s = contentBeatToEditBeat (n->getStartBeat()) + offset;
            auto e = contentBeatToEditBeat (n->getEndBeat()) + offset;
            auto cs = editBeatToContentBeat (s);
            auto ce = editBeatToContentBeat (e);
            copies.add (clip->getSequence().addNote (n->getNoteNumber(), cs, ce - cs, n->getVelocity(), n->getColour(), &um));
        }

        selected = copies;

        // Grow the clip if the copies run past its end.
        auto endTime = edit.tempoSequence.toTime (te::BeatPosition::fromBeats (last + offset));
        if (endTime > clip->getPosition().getEnd())
            clip->setEnd (endTime, true);

        repaint();
    }
}
