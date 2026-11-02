#pragma once

#include "../UI/KnobParameter.h"
#include <juce_core/juce_core.h>
#include <memory>

namespace cdaw
{
    /** What the views of C-Delay, C-Chorus, C-Phaser and C-Filter need from whoever hosts them
        (CDAW's built-in plugin or the VST3): the parameters by ID, undo and the tempo. */
    struct EffectModel
    {
        virtual ~EffectModel() = default;

        virtual std::shared_ptr<KnobParameter> parameter (int id) = 0;

        /** Groups the following changes into one undo step (where the host has undo). */
        virtual void beginUndoTransaction (const juce::String&) {}

        /** The tempo synced times and rates follow. */
        virtual double getBpm() = 0;

        /** Text kept with the plugin's settings that isn't a parameter (a preset's name...). */
        virtual juce::String getText (const juce::String& key)                  { juce::ignoreUnused (key); return {}; }
        virtual void setText (const juce::String& key, const juce::String& value) { juce::ignoreUnused (key, value); }

        float value (int id)                        { return parameter (id)->getValue(); }
        bool isOn (int id)                          { return value (id) >= 0.5f; }

        /** Sets a parameter as one gesture with its own undo step (buttons, menus, presets). */
        void setValue (int id, float newValue, const juce::String& undoName)
        {
            beginUndoTransaction (undoName);
            auto p = parameter (id);
            p->beginGesture();
            p->setValue (newValue);
            p->endGesture();
        }
    };
}
