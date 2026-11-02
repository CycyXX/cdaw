#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/FilterEngine.h"

namespace cdaw::FilterIcons
{
    /** Little pictures for C-Filter's and C-Synth's filter types and LFO shapes (for ChoiceButtons). */
    using Type = FilterEngine::Type;

    inline void drawType (juce::Graphics& g, juce::Rectangle<float> r, int type, juce::Colour c)
    {
        juce::Path p;
        const float x0 = r.getX(), x1 = r.getRight(), top = r.getY() + r.getHeight() * 0.3f, bottom = r.getBottom(), mid = r.getCentreX();

        switch ((Type) type)
        {
            case Type::lowPass:
                p.startNewSubPath (x0, top);
                p.lineTo (mid - 2.0f, top);
                p.quadraticTo (mid + 2.0f, r.getY(), mid + 4.0f, top + 2.0f);
                p.lineTo (x1, bottom);
                break;
            case Type::highPass:
                p.startNewSubPath (x0, bottom);
                p.lineTo (mid - 4.0f, top + 2.0f);
                p.quadraticTo (mid - 2.0f, r.getY(), mid + 2.0f, top);
                p.lineTo (x1, top);
                break;
            case Type::bandPass:
                p.startNewSubPath (x0, bottom);
                p.quadraticTo (mid, r.getY() - 4.0f, x1, bottom);
                break;
            case Type::notch:
            default:
                p.startNewSubPath (x0, top);
                p.lineTo (mid - 5.0f, top);
                p.lineTo (mid, bottom);
                p.lineTo (mid + 5.0f, top);
                p.lineTo (x1, top);
                break;
        }

        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    inline void drawShape (juce::Graphics& g, juce::Rectangle<float> r, int shape, juce::Colour c)
    {
        juce::Path p;
        const int points = 32;
        juce::Random random (7);
        float held = 0.0f;

        for (int i = 0; i <= points; ++i)
        {
            const double phase = (double) i / points;
            if (shape == (int) FilterEngine::Shape::random && i % 6 == 0)
                held = random.nextFloat() * 2.0f - 1.0f;

            const float v = FilterEngine::lfoValue ((FilterEngine::Shape) shape, phase >= 1.0 ? 0.9999 : phase, held);
            const auto x = r.getX() + r.getWidth() * (float) phase;
            const auto y = r.getCentreY() - v * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }

        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}
