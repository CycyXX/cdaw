#include "TakeLanes.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/CompOps.h"

namespace cdaw
{
    TakeLanes::TakeLanes (ArrangeHost& h, te::Track::Ptr t)
        : host (h), viewState (h.getViewState()), track (t)
    {
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
            auto wc = dynamic_cast<te::WaveAudioClip*> (c);
            if (wc == nullptr)
                continue;

            auto files = CompOps::getTakeFiles (*wc);
            if (files.size() < 2)
                continue;

            auto active = CompOps::getActiveTake (*wc);
            auto pos = wc->getPosition();
            auto x1 = (int) viewState.timeToX (pos.getStart());
            auto x2 = (int) viewState.timeToX (pos.getEnd());

            if (x2 < 0 || x1 > getWidth())
                continue;

            for (int row = 0; row < juce::jmin (numRows, files.size()); ++row)
            {
                auto area = juce::Rectangle<int> (x1, row * rowHeight + 2, x2 - x1, rowHeight - 4);
                const bool isActive = row == active;
                auto colour = wc->getColour();

                g.setColour (isActive ? colour.withAlpha (0.55f) : colour.withAlpha (0.15f));
                g.fillRect (area);

                auto& thumb = getThumbnail (files[row]);
                auto visible = area.getIntersection (getLocalBounds());

                if (! visible.isEmpty() && ! thumb.isGeneratingProxy())
                {
                    auto toSource = [&] (int x)
                    {
                        auto t = viewState.xToTime (x);
                        return te::TimePosition::fromSeconds (((t - pos.getStart()) + pos.getOffset()).inSeconds() * wc->getSpeedRatio());
                    };

                    g.setColour (isActive ? Palette::text : Palette::textDim.withAlpha (0.6f));
                    thumb.drawChannels (g, visible, { toSource (visible.getX()), toSource (visible.getRight()) }, wc->getGain());
                }

                if (isActive)
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
            if (auto ct = dynamic_cast<te::ClipTrack*> (track.get()))
                for (auto c : ct->getClips())
                    if (auto wc = dynamic_cast<te::WaveAudioClip*> (c); wc != nullptr && wc->getEditTimeRange().contains (viewState.xToTime (e.x)))
                    {
                        um.beginNewTransaction ("Choose take");
                        CompOps::setActiveTake (*wc, swipeRow);
                    }
        }
        else
        {
            um.beginNewTransaction ("Comp take " + juce::String (swipeRow + 1));
            CompOps::compRange (*track, range, swipeRow);
        }

        swipeRow = -1;
        repaint();
    }
}
