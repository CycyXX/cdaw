#include "TakeLanes.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/GroupEdit.h"

namespace cdaw
{
    TakeLanes::TakeLanes (ArrangeHost& h, te::Track::Ptr t)
        : host (h), viewState (h.getViewState()), track (t)
    {
        setOpaque (true);   // paint() fills the whole component
    }

    TakeLanes::~TakeLanes() = default;

    te::SmartThumbnail& TakeLanes::getThumbnail (const juce::File& f)
    {
        auto& slot = thumbnails[f.getFullPathName()];

        if (slot == nullptr)
            slot = std::make_unique<te::SmartThumbnail> (track->edit.engine, te::AudioFile (track->edit.engine, f), *this, &track->edit);

        return *slot;
    }

    void TakeLanes::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::laneAlternate.darker (0.15f));
        auto ct = dynamic_cast<te::ClipTrack*> (track.get());
        if (ct == nullptr)
            return;

        const int numRows = getHeight() / rowHeight;

        for (int row = 0; row < numRows; ++row)
        {
            auto rowArea = juce::Rectangle<int> (0, row * rowHeight, getWidth(), rowHeight);
            g.setColour (row == hoverRow ? Palette::panelRaised : (row % 2 == 0 ? Palette::laneBackground : Palette::laneAlternate));
            g.fillRect (rowArea.reduced (0, 1));
        }

        for (auto c : ct->getClips())
        {
            if (! CompOps::hasTakes (*c))
                continue;

            auto clipRange = c->getEditTimeRange();

            if (viewState.timeToX (clipRange.getEnd()) < 0 || viewState.timeToX (clipRange.getStart()) > getWidth())
                continue;

            auto colour = c->getColour();
            auto* audio = dynamic_cast<te::AudioClipBase*> (c);

            for (auto& take : CompOps::getTakes (*c))
            {
                // Under this piece, as far as the take has material (passes can be shorter than the clip).
                auto shown = clipRange.getIntersectionWith (take.range);

                if (take.row >= numRows || shown.isEmpty())
                    continue;

                auto x1 = (int) viewState.timeToX (shown.getStart());
                auto x2 = (int) viewState.timeToX (shown.getEnd());
                auto area = juce::Rectangle<int> (x1, take.row * rowHeight + 2, x2 - x1, rowHeight - 4);

                g.setColour (take.active ? colour.withAlpha (0.55f) : colour.withAlpha (0.15f));
                g.fillRect (area);

                auto visible = area.getIntersection (getLocalBounds());

                if (take.midi != nullptr)
                {
                    drawNotes (g, take, visible, shown);
                }
                else if (! visible.isEmpty() && audio != nullptr)
                {
                    auto& thumb = getThumbnail (take.file);

                    if (! thumb.isGeneratingProxy())
                    {
                        auto toSource = [&] (int x)
                        {
                            return te::TimePosition::fromSeconds ((viewState.xToTime (x) - take.range.getStart()).inSeconds() * audio->getSpeedRatio());
                        };

                        g.setColour (take.active ? Palette::text : Palette::textDim.withAlpha (0.6f));
                        thumb.drawChannels (g, visible, { toSource (visible.getX()), toSource (visible.getRight()) }, audio->getGain());
                    }
                }

                if (take.active)
                {
                    g.setColour (colour);
                    g.drawRect (area, 1);
                }
            }
        }

        // Row labels on the left
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (int row = 0; row < numRows; ++row)
        {
            g.setColour (Palette::text.withAlpha (0.8f));
            g.drawText ("Take " + juce::String (row + 1), 4, row * rowHeight, 60, rowHeight, juce::Justification::centredLeft);
        }

        // Swipe in progress
        if (swipeRow >= 0)
        {
            auto a = (int) viewState.timeToX (juce::jmin (swipeStart, swipeEnd));
            auto b = (int) viewState.timeToX (juce::jmax (swipeStart, swipeEnd));
            g.setColour (Palette::accent.withAlpha (0.35f));
            g.fillRect (a, swipeRow * rowHeight, juce::jmax (1, b - a), rowHeight);
            g.setColour (Palette::accent);
            g.drawRect (a, swipeRow * rowHeight, juce::jmax (1, b - a), rowHeight);
        }

        g.setColour (Palette::windowBackground);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void TakeLanes::drawNotes (juce::Graphics& g, const CompOps::TakeInfo& take, juce::Rectangle<int> area, te::TimeRange shown)
    {
        auto& notes = take.midi->getNotes();

        if (area.isEmpty() || notes.isEmpty())
            return;

        int lo = 127, hi = 0;

        for (auto n : notes)
        {
            lo = juce::jmin (lo, n->getNoteNumber());
            hi = juce::jmax (hi, n->getNoteNumber());
        }

        auto& ts = track->edit.tempoSequence;
        auto range = (float) juce::jmax (12, hi - lo + 1);
        auto noteH = juce::jmax (1.0f, (float) area.getHeight() / range);
        auto colour = take.active ? Palette::text : Palette::textDim.withAlpha (0.7f);

        for (auto n : notes)
        {
            auto s = ts.toTime (take.midiStart + te::BeatDuration::fromBeats (n->getStartBeat().inBeats()));
            auto e = ts.toTime (take.midiStart + te::BeatDuration::fromBeats (n->getEndBeat().inBeats()));

            if (e <= shown.getStart() || s >= shown.getEnd())
                continue;

            auto x1 = (float) viewState.timeToX (juce::jmax (s, shown.getStart()));
            auto x2 = (float) viewState.timeToX (juce::jmin (e, shown.getEnd()));
            auto y = (float) area.getBottom() - (float) (n->getNoteNumber() - lo + 1) * noteH;

            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (x1, y, juce::jmax (1.5f, x2 - x1), noteH));
        }
    }

    void TakeLanes::mouseMove (const juce::MouseEvent& e)
    {
        auto row = rowAt (e.y);
        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }

        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }

    void TakeLanes::mouseExit (const juce::MouseEvent&)
    {
        hoverRow = -1;
        repaint();
    }

    void TakeLanes::mouseDown (const juce::MouseEvent& e)
    {
        swipeRow = rowAt (e.y);
        auto t = viewState.xToTime (e.x);
        swipeStart = swipeEnd = e.mods.isShiftDown() ? t : viewState.snap (t);
        repaint();
    }

    void TakeLanes::mouseDrag (const juce::MouseEvent& e)
    {
        if (swipeRow < 0)
            return;

        auto t = viewState.xToTime (e.x);
        swipeEnd = e.mods.isShiftDown() ? t : viewState.snap (t);
        repaint();
    }

    void TakeLanes::mouseUp (const juce::MouseEvent& e)
    {
        if (swipeRow < 0)
            return;

        auto& um = track->edit.getUndoManager();
        auto range = te::TimeRange (juce::jmin (swipeStart, swipeEnd), juce::jmax (swipeStart, swipeEnd));

        if (! e.mouseWasDraggedSinceMouseDown() || range.getLength() < te::TimeDuration::fromSeconds (0.005))
        {
            // Click: use this take for the whole clip piece under the mouse.
            um.beginNewTransaction ("Choose take");
            GroupEdit::useTakeAt (*track, viewState.xToTime (e.x), swipeRow);
        }
        else
        {
            um.beginNewTransaction ("Comp take " + juce::String (swipeRow + 1));
            GroupEdit::compRange (*track, range, swipeRow);
        }

        swipeRow = -1;
        repaint();
    }
}
