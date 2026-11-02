#include "NotesPage.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/ArrangerOps.h"
#include "../../Model/SongNotes.h"

namespace cdaw
{
    namespace
    {
        constexpr int rowHeaderHeight = 22;
        constexpr int commitDelayMs = 700;

        void styleEditor (juce::TextEditor& e, const juce::String& placeholder)
        {
            e.setMultiLine (true, true);
            e.setReturnKeyStartsNewLine (true);
            e.setScrollbarsShown (true);
            e.setFont (juce::FontOptions (14.0f));
            e.setColour (juce::TextEditor::backgroundColourId, Palette::laneBackground);
            e.setColour (juce::TextEditor::outlineColourId, Palette::panelOutline);
            e.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
            e.setColour (juce::TextEditor::textColourId, Palette::text);
            e.setTextToShowWhenEmpty (placeholder, Palette::textDisabled);
        }
    }

    //==============================================================================
    /** One section on the lyric sheet: its name strip and its lyrics. */
    struct NotesPage::SectionRow : public juce::Component
    {
        SectionRow (NotesPage& o, te::ArrangerClip& s) : owner (o), section (&s)
        {
            styleEditor (editor, "Lyrics for " + s.getName());
            editor.setScrollbarsShown (false);   // the row grows with its text instead
            editor.setText (SongNotes::getLyrics (s), false);
            editor.onTextChange = [this]
            {
                dirty = true;
                owner.lastEditTime = juce::Time::getMillisecondCounter();
                owner.layoutRows();
            };
            editor.onFocusLost = [this] { commit(); };
            editor.onEscapeKey = [this] { commit(); owner.grabKeyboardFocus(); };
            addAndMakeVisible (editor);
        }

        void commit()
        {
            if (! dirty || section == nullptr || section->getClipTrack() == nullptr)
                return;

            dirty = false;
            auto& um = owner.edit.getUndoManager();
            um.beginNewTransaction ("Edit lyrics");
            SongNotes::setLyrics (*section, editor.getText(), &um);
        }

        /** The section's lyrics changed elsewhere (undo, a copied section): show them, unless being typed into. */
        void refreshText()
        {
            if (! dirty && ! editor.hasKeyboardFocus (true) && section != nullptr)
            {
                auto text = SongNotes::getLyrics (*section);
                if (editor.getText() != text)
                    editor.setText (text, false);
            }
        }

        int getIdealHeight() const
        {
            // Count the lines as they wrap (the editor's own text height never drops below its size).
            juce::Font font (juce::FontOptions (14.0f));
            auto width = (float) juce::jmax (60, editor.getWidth() - 16);
            juce::StringArray paragraphs;
            paragraphs.addLines (editor.getText());
            int lines = 0;

            for (auto& p : paragraphs)
                lines += juce::jmax (1, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, p) / width));

            lines = juce::jmax (3, lines + 1);   // a free line to type into
            return rowHeaderHeight + juce::roundToInt ((float) lines * font.getHeight()) + 16 + 8;
        }

        void paint (juce::Graphics& g) override
        {
            auto header = getLocalBounds().removeFromTop (rowHeaderHeight);
            auto colour = section->getColour();

            if (current)
            {
                g.setColour (colour.withAlpha (0.12f));
                g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            }

            g.setColour (colour);
            g.fillRoundedRectangle (header.removeFromLeft (6).reduced (0, 4).toFloat(), 2.0f);
            header.removeFromLeft (6);

            g.setColour (current ? Palette::text : Palette::textDim);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            auto name = section->getName();
            g.drawText (name, header, juce::Justification::centredLeft, true);

            auto& ts = owner.edit.tempoSequence;
            auto startBar = ts.toBarsAndBeats (section->getPosition().getStart()).bars + 1;
            auto bars = juce::roundToInt (ts.toBarsAndBeats (section->getPosition().getEnd()).getTotalBars()
                                          - ts.toBarsAndBeats (section->getPosition().getStart()).getTotalBars());
            auto nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (Palette::textDim);
            g.drawText ("bar " + juce::String (startBar) + ", " + juce::String (bars) + (bars == 1 ? " bar" : " bars"),
                        header.withTrimmedLeft (nameWidth + 10), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            editor.setBounds (getLocalBounds().withTrimmedTop (rowHeaderHeight).withTrimmedBottom (8));
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            // Clicking the name jumps to the section.
            if (e.y < rowHeaderHeight && ! e.mouseWasDraggedSinceMouseDown())
                owner.edit.getTransport().setPosition (section->getPosition().getStart());
        }

        NotesPage& owner;
        te::ArrangerClip::Ptr section;
        juce::TextEditor editor;
        bool dirty = false, current = false;
    };

    /** The viewed component holding the rows. */
    struct NotesPage::RowList : public juce::Component
    {
        explicit RowList (NotesPage& o) : owner (o) {}

        void paint (juce::Graphics& g) override
        {
            if (owner.rows.isEmpty())
            {
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (13.0f));
                g.drawFittedText ("No song sections yet. Add them in the Arranger strip under the ruler "
                                  "(Edit > Add Song Section): each one gets its own lyrics here.",
                                  getLocalBounds().reduced (12), juce::Justification::topLeft, 4);
            }
        }

        NotesPage& owner;
    };

    //==============================================================================
    NotesPage::NotesPage (EditViewState& vs) : viewState (vs), edit (vs.edit)
    {
        setOpaque (true);

        styleEditor (notesEditor, "Ideas, chords, to-dos, a first draft of the lyrics... Saved with the project.");
        notesEditor.setText (SongNotes::getProjectNotes (edit), false);
        notesEditor.onTextChange = [this]
        {
            notesDirty = true;
            lastEditTime = juce::Time::getMillisecondCounter();
        };
        notesEditor.onFocusLost = [this] { commitPendingEdits(); };
        addAndMakeVisible (notesEditor);

        copyButton.setTooltip ("Copy every section's lyrics, with the section names, to paste elsewhere");
        copyButton.onClick = [this]
        {
            commitPendingEdits();
            juce::SystemClipboard::copyTextToClipboard (getLyricSheet());
        };
        addAndMakeVisible (copyButton);

        rowList = std::make_unique<RowList> (*this);
        viewport.setViewedComponent (rowList.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        if (auto track = ArrangerOps::getTrack (edit))
        {
            arrangerState = track->state;
            arrangerState.addListener (this);
        }

        rebuildRows();
        startTimer (150);
    }

    NotesPage::~NotesPage()
    {
        commitPendingEdits();
        arrangerState.removeListener (this);
    }

    void NotesPage::commitPendingEdits()
    {
        if (notesDirty)
        {
            notesDirty = false;
            SongNotes::setProjectNotes (edit, notesEditor.getText());
        }

        for (auto* r : rows)
            r->commit();
    }

    juce::String NotesPage::getLyricSheet() const
    {
        juce::String sheet;

        for (auto s : ArrangerOps::getSections (edit))
        {
            auto lyrics = SongNotes::getLyrics (*s).trim();

            if (lyrics.isNotEmpty())
                sheet << "[" << s->getName() << "]" << juce::newLine << lyrics << juce::newLine << juce::newLine;
        }

        return sheet.trimEnd();
    }

    void NotesPage::focusSection (te::ArrangerClip& section)
    {
        if (needsRebuild)
            rebuildRows();

        for (auto* r : rows)
        {
            if (r->section.get() == &section)
            {
                viewport.setViewPosition (0, juce::jmax (0, r->getY() - 8));
                r->editor.grabKeyboardFocus();
                r->editor.moveCaretToEnd();
                return;
            }
        }
    }

    void NotesPage::sectionsChanged()
    {
        // Rows are rebuilt on the timer, not while a ValueTree callback is running (or mid-typing).
        needsRebuild = true;
    }

    void NotesPage::rebuildRows()
    {
        needsRebuild = false;
        auto sections = ArrangerOps::getSections (edit);

        // Same sections in the same order: keep the rows (and whatever is being typed).
        bool same = sections.size() == rows.size();
        for (int i = 0; same && i < rows.size(); ++i)
            same = rows[i]->section.get() == sections[i];

        if (same)
        {
            for (auto* r : rows)
            {
                r->refreshText();
                r->repaint();
            }

            layoutRows();
            return;
        }

        // Keep typing that hasn't been written yet.
        for (auto* r : rows)
            r->commit();

        rows.clear();

        for (auto s : sections)
            rowList->addAndMakeVisible (rows.add (new SectionRow (*this, *s)));

        layoutRows();
        updateCurrentSection();
    }

    void NotesPage::layoutRows()
    {
        auto width = viewport.getMaximumVisibleWidth();
        int y = 4;

        for (auto* r : rows)
        {
            // Width first: the text's height depends on where it wraps.
            r->setBounds (4, y, juce::jmax (100, width - 8), juce::jmax (r->getHeight(), rowHeaderHeight + 40));
            auto h = r->getIdealHeight();
            r->setSize (r->getWidth(), h);
            y += h + 4;
        }

        rowList->setSize (juce::jmax (100, width), juce::jmax (y, viewport.getHeight()));
        rowList->repaint();
    }

    void NotesPage::updateCurrentSection()
    {
        auto now = ArrangerOps::findSectionAt (edit, edit.getTransport().getPosition());

        for (auto* r : rows)
        {
            const bool isCurrent = r->section.get() == now;

            if (r->current != isCurrent)
            {
                r->current = isCurrent;
                r->repaint();

                // Follow the song while it plays (unless the sheet is being typed into).
                if (isCurrent && edit.getTransport().isPlaying() && ! rowList->hasKeyboardFocus (true))
                {
                    auto top = viewport.getViewPositionY();
                    if (r->getY() < top || r->getBottom() > top + viewport.getHeight())
                        viewport.setViewPosition (0, juce::jmax (0, r->getY() - 8));
                }
            }
        }
    }

    void NotesPage::timerCallback()
    {
        // Write what was typed once the typing pauses.
        auto anyDirty = notesDirty;
        for (auto* r : rows)
            anyDirty = anyDirty || r->dirty;

        if (anyDirty && juce::Time::getMillisecondCounter() - lastEditTime > (juce::uint32) commitDelayMs)
            commitPendingEdits();

        if (needsRebuild)
            rebuildRows();

        if (isShowing())
            updateCurrentSection();
    }

    void NotesPage::visibilityChanged()
    {
        if (! isVisible())
            commitPendingEdits();
    }

    void NotesPage::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("SONG NOTES", notesHeader, juce::Justification::centredLeft);
        g.drawText ("LYRICS BY SECTION", lyricsHeader, juce::Justification::centredLeft);
    }

    void NotesPage::resized()
    {
        auto r = getLocalBounds().reduced (8, 4);
        auto left = r.removeFromLeft (juce::jlimit (240, 520, r.getWidth() * 2 / 5));
        r.removeFromLeft (10);

        notesHeader = left.removeFromTop (24);
        notesEditor.setBounds (left);

        lyricsHeader = r.removeFromTop (24);
        copyButton.setBounds (lyricsHeader.removeFromRight (130).reduced (0, 2));
        viewport.setBounds (r);
        layoutRows();
    }
}
