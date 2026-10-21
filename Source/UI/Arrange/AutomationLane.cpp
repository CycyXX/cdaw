#include "AutomationLane.h"
#include "TimeRuler.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/AutomationOps.h"

namespace cdaw
{
    //==============================================================================
    AutomationLaneHeader::AutomationLaneHeader (ArrangeHost& h, te::Track::Ptr t, te::AutomatableParameter::Ptr p)
        : host (h), track (t), param (p)
    {
        paramButton.setButtonText (AutomationOps::getDisplayName (*param));
        paramButton.setTooltip ("Choose the parameter for this lane");
        paramButton.onClick = [this]
        {
            juce::PopupMenu m;
            juce::String lastPlugin;

            for (auto p2 : AutomationOps::getAutomatableParameters (*track))
            {
                auto key = AutomationOps::getKey (*p2);
                auto oldKey = AutomationOps::getKey (*param);
                te::Track::Ptr tr = track;
                m.addItem (AutomationOps::getDisplayName (*p2) + (p2->hasAutomationPoints() ? "  *" : ""), true, p2 == param.get(), [tr, key, oldKey]
                {
                    auto keys = AutomationOps::getLaneKeys (*tr);
                    auto idx = keys.indexOf (oldKey);
                    if (idx >= 0 && ! keys.contains (key))
                        keys.set (idx, key);
                    AutomationOps::setLaneKeys (*tr, keys);
                });
            }

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&paramButton));
        };
        addAndMakeVisible (paramButton);

        modeButton.setTooltip ("Automation mode for this track: Read plays automation; Touch/Latch/Write record it while 'Write automation' is on");
        modeButton.onClick = [this]
        {
            juce::PopupMenu m;
            for (auto mode : { te::AutomationMode::read, te::AutomationMode::touch, te::AutomationMode::latch, te::AutomationMode::write })
                m.addItem (AutomationOps::getModeName (mode), true, track->automationMode.get() == mode, [this, mode]
                {
                    track->automationMode = mode;
                    if (mode != te::AutomationMode::read)
                        track->edit.getAutomationRecordManager().setWritingAutomation (true);
                });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modeButton));
        };
        addAndMakeVisible (modeButton);

        closeButton.setTooltip ("Hide this lane (automation is kept)");
        closeButton.onClick = [this] { AutomationOps::hideLane (*track, AutomationOps::getKey (*param)); };
        addAndMakeVisible (closeButton);

        valueLabel.setJustificationType (juce::Justification::centredRight);
        valueLabel.setColour (juce::Label::textColourId, Palette::textDim);
        valueLabel.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (valueLabel);

        timerCallback();
        startTimerHz (8);
    }

    void AutomationLaneHeader::timerCallback()
    {
        modeButton.setButtonText (AutomationOps::getModeName (track->automationMode.get()));
        valueLabel.setText (param->getCurrentValueAsStringWithLabel(), juce::dontSendNotification);
    }

    void AutomationLaneHeader::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground.darker (0.15f));
        g.setColour (track->getColour().withAlpha (0.5f));
        g.fillRect (0, 0, 3, getHeight());
        g.setColour (Palette::windowBackground);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.setColour (Palette::panelOutline);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void AutomationLaneHeader::resized()
    {
        auto r = getLocalBounds().reduced (6, 4).withTrimmedLeft (10);
        auto top = r.removeFromTop (20);
        closeButton.setBounds (top.removeFromRight (20));
        top.removeFromRight (2);
        paramButton.setBounds (top);

        if (r.getHeight() >= 18)
        {
            r.removeFromTop (3);
            auto row = r.removeFromTop (18);
            modeButton.setBounds (row.removeFromLeft (64));
            valueLabel.setBounds (row);
        }
    }

    //==============================================================================
    AutomationLane::AutomationLane (ArrangeHost& h, te::Track::Ptr t, te::AutomatableParameter::Ptr p)
        : host (h), viewState (h.getViewState()), track (t), param (p)
    {
        param->addListener (this);
        viewState.state.addListener (this);
    }

    AutomationLane::~AutomationLane()
    {
        viewState.state.removeListener (this);
        param->removeListener (this);
    }

    void AutomationLane::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& id)
    {
        if (id == ViewIDs::viewStart || id == ViewIDs::pixelsPerSecond || id == ViewIDs::gridIndex)
            repaint();
    }

    float AutomationLane::valueToY (float value) const
    {
        auto prop = AutomationOps::toProportion (*param, value);
        return 4.0f + (1.0f - prop) * (getHeight() - 8.0f);
    }

    float AutomationLane::yToValue (float y) const
    {
        return AutomationOps::fromProportion (*param, 1.0f - (y - 4.0f) / juce::jmax (1.0f, getHeight() - 8.0f));
    }

    te::TimePosition AutomationLane::xToTimeSnapped (float x, const juce::ModifierKeys& mods) const
    {
        auto t = viewState.xToTime (x);
        return mods.isShiftDown() ? t : viewState.snap (t);
    }

    int AutomationLane::pointAt (juce::Point<float> p) const
    {
        auto& curve = param->getCurve();

        for (int i = curve.getNumPoints(); --i >= 0;)
        {
            auto x = (float) viewState.timeToX (curve.getPointTime (i));
            auto y = valueToY (curve.getPointValue (i));

            if (p.getDistanceFrom ({ x, y }) <= 6.0f)
                return i;
        }

        return -1;
    }

    void AutomationLane::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::laneAlternate);
        GridPainter::drawGridLines (g, viewState, getLocalBounds());

        auto& curve = param->getCurve();
        const auto colour = Palette::automation;
        auto w = (float) getWidth();

        // Default / current value line when there's no automation.
        if (curve.getNumPoints() == 0)
        {
            auto y = valueToY (param->getCurrentValue());
            g.setColour (colour.withAlpha (0.45f));
            g.drawHorizontalLine ((int) y, 0.0f, w);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Click to add points" + juce::String (viewState.getTool() == Tool::draw ? ", drag to draw" : ""),
                        getLocalBounds().reduced (8, 2), juce::Justification::topLeft);
        }
        else
        {
            // Sample the curve per pixel so curved segments draw correctly.
            juce::Path p;
            bool started = false;

            for (float x = 0.0f; x <= w; x += 2.0f)
            {
                auto t = viewState.xToTime (x);
                auto y = valueToY (curve.getValueAt (t, param->getCurrentValue()));

                if (! started) { p.startNewSubPath (x, y); started = true; }
                else           p.lineTo (x, y);
            }

            juce::Path fill (p);
            fill.lineTo (w, (float) getHeight());
            fill.lineTo (0.0f, (float) getHeight());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (fill);

            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f));

            for (int i = 0; i < curve.getNumPoints(); ++i)
            {
                auto x = (float) viewState.timeToX (curve.getPointTime (i));
                if (x < -6 || x > w + 6)
                    continue;

                auto y = valueToY (curve.getPointValue (i));
                auto r = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, y });
                g.setColour (i == hoverIndex || i == dragIndex ? juce::Colours::white : colour);
                g.fillEllipse (r);
                g.setColour (Palette::windowBackground);
                g.drawEllipse (r, 1.0f);
            }
        }

        g.setColour (Palette::windowBackground);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, w);
    }

    void AutomationLane::mouseMove (const juce::MouseEvent& e)
    {
        auto idx = pointAt (e.position);
        if (idx != hoverIndex)
        {
            hoverIndex = idx;
            repaint();
        }

        setMouseCursor (idx >= 0 ? juce::MouseCursor::DraggingHandCursor
                                 : viewState.getTool() == Tool::draw ? juce::MouseCursor::CrosshairCursor
                                                                     : juce::MouseCursor::NormalCursor);
    }

    void AutomationLane::mouseDown (const juce::MouseEvent& e)
    {
        auto& curve = param->getCurve();
        auto& um = track->edit.getUndoManager();
        auto idx = pointAt (e.position);
        dragIndex = -1;
        bending = drawing = false;

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("Delete point", idx >= 0, false, [this, idx]
            {
                track->edit.getUndoManager().beginNewTransaction ("Delete automation point");
                param->getCurve().removePoint (idx, &track->edit.getUndoManager());
            });
            m.addItem ("Delete points in selected range", viewState.hasRange(), false, [this]
            {
                track->edit.getUndoManager().beginNewTransaction ("Delete automation");
                param->getCurve().removePointsInRegion (viewState.getRange(), &track->edit.getUndoManager());
            });
            m.addItem ("Clear lane", curve.getNumPoints() > 0, false, [this]
            {
                track->edit.getUndoManager().beginNewTransaction ("Clear automation");
                param->getCurve().clear (&track->edit.getUndoManager());
            });
            m.addItem ("Simplify", curve.getNumPoints() > 2, false, [this]
            {
                track->edit.getUndoManager().beginNewTransaction ("Simplify automation");
                param->getCurve().simplify (te::TimeRange (te::TimePosition(), te::Edit::getMaximumEditEnd()), 0.01, 0.002f, &track->edit.getUndoManager());
            });
            m.addSeparator();
            m.addItem ("Hide lane", [this] { AutomationOps::hideLane (*track, AutomationOps::getKey (*param)); });
            m.showMenuAsync ({});
            return;
        }

        if (viewState.getTool() == Tool::erase)
        {
            if (idx >= 0)
            {
                um.beginNewTransaction ("Delete automation point");
                curve.removePoint (idx, &um);
            }
            return;
        }

        um.beginNewTransaction ("Edit automation");

        if (idx >= 0)
        {
            if (e.mods.isAltDown())
            {
                curve.removePoint (idx, &um);
                return;
            }

            dragIndex = idx;
            return;
        }

        // Ctrl-drag on a segment bends its curve.
        if (e.mods.isCommandDown() && curve.getNumPoints() > 1)
        {
            auto before = curve.indexBefore (viewState.xToTime (e.position.x));
            if (before >= 0 && before < curve.getNumPoints() - 1)
            {
                dragIndex = before;
                bending = true;
                bendStartCurve = curve.getPointCurve (before);
                return;
            }
        }

        if (viewState.getTool() == Tool::draw)
        {
            drawing = true;
            lastDrawTime = viewState.xToTime (e.position.x);
            curve.addPoint (lastDrawTime, yToValue (e.position.y), 0.0f, &um);
            return;
        }

        auto t = xToTimeSnapped (e.position.x, e.mods);
        dragIndex = curve.addPoint (t, yToValue (e.position.y), 0.0f, &um);
    }

    void AutomationLane::mouseDrag (const juce::MouseEvent& e)
    {
        auto& curve = param->getCurve();
        auto& um = track->edit.getUndoManager();

        if (drawing)
        {
            auto t = viewState.xToTime (e.position.x);
            auto a = juce::jmin (t, lastDrawTime), b = juce::jmax (t, lastDrawTime);

            if (b - a > te::TimeDuration::fromSeconds (0.01))
            {
                curve.removePointsInRegion ({ a + te::TimeDuration::fromSeconds (0.0001), b }, &um);
                curve.addPoint (t, yToValue (e.position.y), 0.0f, &um);
                lastDrawTime = t;
            }
            return;
        }

        if (dragIndex < 0 || dragIndex >= curve.getNumPoints())
            return;

        if (bending)
        {
            auto delta = (float) -e.getDistanceFromDragStartY() / 80.0f;
            curve.setCurveValue (dragIndex, juce::jlimit (-1.0f, 1.0f, bendStartCurve + delta), &um);
            return;
        }

        // Keep points in order: clamp between neighbours.
        auto t = xToTimeSnapped (e.position.x, e.mods);
        if (dragIndex > 0)
            t = juce::jmax (t, curve.getPointTime (dragIndex - 1));
        if (dragIndex < curve.getNumPoints() - 1)
            t = juce::jmin (t, curve.getPointTime (dragIndex + 1));

        dragIndex = curve.movePoint (*param, dragIndex, t, yToValue (juce::jlimit (0.0f, (float) getHeight(), e.position.y)), false, &um);
    }

    void AutomationLane::mouseUp (const juce::MouseEvent&)
    {
        if (drawing)
            param->getCurve().removeRedundantPoints (te::TimeRange (te::TimePosition(), te::Edit::getMaximumEditEnd()), &track->edit.getUndoManager());

        dragIndex = -1;
        bending = drawing = false;
        repaint();
    }

    void AutomationLane::mouseDoubleClick (const juce::MouseEvent& e)
    {
        auto idx = pointAt (e.position);
        if (idx >= 0)
        {
            track->edit.getUndoManager().beginNewTransaction ("Delete automation point");
            param->getCurve().removePoint (idx, &track->edit.getUndoManager());
        }
    }
}
