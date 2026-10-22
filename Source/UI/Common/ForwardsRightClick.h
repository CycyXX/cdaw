#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace cdaw
{
    /** A control (button, knob, meter...) whose right-click goes to `onRightClick` instead of
        acting as a click: a right-click on a mute button opens the track menu rather than muting.
        Left clicks and drags behave as usual. */
    template <typename Base>
    class ForwardsRightClick : public Base
    {
    public:
        using Base::Base;

        std::function<void (const juce::MouseEvent&)> onRightClick;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick != nullptr)
            {
                onRightClick (e);
                return;
            }

            Base::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! (e.mods.isPopupMenu() && onRightClick != nullptr))
                Base::mouseDrag (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! (e.mods.isPopupMenu() && onRightClick != nullptr))
                Base::mouseUp (e);
        }
    };
}
