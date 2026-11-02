#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** The song's key and scale (e.g. A Minor, E Phrygian Dominant). The piano roll shades the
        notes outside it and can snap notes onto it. Stored on the edit, so changing it is undoable;
        a project without one has no key (nothing is shaded). */
    namespace KeyScale
    {
        inline const juce::Identifier rootID ("cdawKeyRoot");     // 0 = C ... 11 = B
        inline const juce::Identifier scaleID ("cdawKeyScale");   // a name from getScales()

        struct Scale
        {
            juce::String name;
            juce::Array<int> intervals;   // semitones above the root, ascending, starting at 0
        };

        /** The scales on offer, in menu order. */
        const juce::Array<Scale>& getScales();
        const Scale* findScale (const juce::String& name);

        struct Key
        {
            int root = -1;                // -1: no key set
            juce::String scale;

            bool isSet() const              { return root >= 0 && findScale (scale) != nullptr; }
            bool contains (int note) const;
            bool isRoot (int note) const    { return isSet() && ((note % 12) + 12) % 12 == root; }
            juce::String getName() const;   // "A Minor", or "No key"
            juce::String getShortName() const;   // "A min", or "-"
        };

        Key getKey (const te::Edit&);
        void setKey (te::Edit&, const Key&, juce::UndoManager*);

        /** The nearest note in the key (ties go `direction` way: +1 up, -1 down). Unchanged without a key. */
        int snapToKey (const Key&, int note, int direction = 1);

        /** The next note in the key strictly above (`steps` > 0) or below, `steps` scale steps away. */
        int stepInKey (const Key&, int note, int steps);

        juce::String getNoteName (int root);   // "C", "C#" ... "B"

        /** Root and scale submenus plus "No key", acting on the edit (undoable). */
        juce::PopupMenu createMenu (te::Edit&);

        /** The snap-to-key switch, per project (view state, not undoable). */
        bool isSnapEnabled (te::Edit&);
        void setSnapEnabled (te::Edit&, bool);
    }
}
