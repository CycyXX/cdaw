#include "KeyScale.h"
#include "EditViewState.h"

namespace cdaw::KeyScale
{
    namespace
    {
        const juce::Identifier snapID ("cdawScaleSnap");

        int pitchClass (int note)   { return ((note % 12) + 12) % 12; }
    }

    const juce::Array<Scale>& getScales()
    {
        static const juce::Array<Scale> scales {
            Scale { "Major",              { 0, 2, 4, 5, 7, 9, 11 } },
            Scale { "Minor",              { 0, 2, 3, 5, 7, 8, 10 } },
            Scale { "Harmonic Minor",     { 0, 2, 3, 5, 7, 8, 11 } },
            Scale { "Melodic Minor",      { 0, 2, 3, 5, 7, 9, 11 } },
            Scale { "Dorian",             { 0, 2, 3, 5, 7, 9, 10 } },
            Scale { "Phrygian",           { 0, 1, 3, 5, 7, 8, 10 } },
            Scale { "Phrygian Dominant",  { 0, 1, 4, 5, 7, 8, 10 } },
            Scale { "Lydian",             { 0, 2, 4, 6, 7, 9, 11 } },
            Scale { "Mixolydian",         { 0, 2, 4, 5, 7, 9, 10 } },
            Scale { "Locrian",            { 0, 1, 3, 5, 6, 8, 10 } },
            Scale { "Hungarian Minor",    { 0, 2, 3, 6, 7, 8, 11 } },
            Scale { "Minor Pentatonic",   { 0, 3, 5, 7, 10 } },
            Scale { "Major Pentatonic",   { 0, 2, 4, 7, 9 } },
            Scale { "Blues",              { 0, 3, 5, 6, 7, 10 } },
        };
        return scales;
    }

    const Scale* findScale (const juce::String& name)
    {
        for (auto& s : getScales())
            if (s.name.equalsIgnoreCase (name))
                return &s;

        return nullptr;
    }

    bool Key::contains (int note) const
    {
        if (! isSet())
            return true;

        return findScale (scale)->intervals.contains (pitchClass (note - root));
    }

    juce::String getNoteName (int root)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[pitchClass (root)];
    }

    juce::String Key::getName() const
    {
        return isSet() ? getNoteName (root) + " " + findScale (scale)->name : juce::String ("No key");
    }

    juce::String Key::getShortName() const
    {
        if (! isSet())
            return "-";

        auto s = findScale (scale)->name;
        if (s == "Major")   s = "maj";
        if (s == "Minor")   s = "min";
        return getNoteName (root) + " " + s;
    }

    Key getKey (const te::Edit& edit)
    {
        if (! edit.state.hasProperty (rootID))
            return {};

        return { juce::jlimit (0, 11, (int) edit.state[rootID]), edit.state[scaleID].toString() };
    }

    void setKey (te::Edit& edit, const Key& key, juce::UndoManager* um)
    {
        if (key.isSet())
        {
            edit.state.setProperty (rootID, pitchClass (key.root), um);
            edit.state.setProperty (scaleID, findScale (key.scale)->name, um);
        }
        else
        {
            edit.state.removeProperty (rootID, um);
            edit.state.removeProperty (scaleID, um);
        }
    }

    int snapToKey (const Key& key, int note, int direction)
    {
        if (! key.isSet() || key.contains (note))
            return note;

        // Scales here leave gaps of at most three semitones, so the nearest note is close.
        for (int distance = 1; distance <= 6; ++distance)
        {
            auto first = note + (direction >= 0 ? distance : -distance);
            auto second = note - (direction >= 0 ? distance : -distance);

            if (juce::isPositiveAndBelow (first, 128) && key.contains (first))   return first;
            if (juce::isPositiveAndBelow (second, 128) && key.contains (second)) return second;
        }

        return note;
    }

    int stepInKey (const Key& key, int note, int steps)
    {
        if (! key.isSet())
            return juce::jlimit (0, 127, note + steps);

        auto dir = steps > 0 ? 1 : -1;

        for (int i = 0; i < std::abs (steps); ++i)
        {
            auto next = note + dir;
            while (juce::isPositiveAndBelow (next, 128) && ! key.contains (next))
                next += dir;

            if (! juce::isPositiveAndBelow (next, 128))
                break;

            note = next;
        }

        return note;
    }

    juce::PopupMenu createMenu (te::Edit& edit)
    {
        auto key = getKey (edit);
        auto* um = &edit.getUndoManager();
        juce::PopupMenu m;

        auto apply = [&edit, um] (Key k)
        {
            um->beginNewTransaction ("Change key");
            setKey (edit, k, um);
        };

        juce::PopupMenu roots;
        for (int r = 0; r < 12; ++r)
        {
            // Picking a root keeps the scale (Minor when there was no key yet).
            Key k { r, key.isSet() ? key.scale : juce::String ("Minor") };
            roots.addItem (getNoteName (r), true, key.isSet() && key.root == r, [apply, k] { apply (k); });
        }

        juce::PopupMenu scales;
        for (auto& s : getScales())
        {
            Key k { key.isSet() ? key.root : 9, s.name };   // A when there was no key yet
            scales.addItem (s.name, true, key.isSet() && key.scale == s.name, [apply, k] { apply (k); });

            if (s.name == "Hungarian Minor")
                scales.addSeparator();
        }

        m.addSectionHeader ("Key: " + key.getName());
        m.addSubMenu ("Root", roots);
        m.addSubMenu ("Scale", scales);
        m.addSeparator();
        m.addItem ("No Key", key.isSet(), false, [apply] { apply ({}); });
        return m;
    }

    bool isSnapEnabled (te::Edit& edit)
    {
        return (bool) edit.state.getChildWithName (ViewIDs::CDAWVIEW).getProperty (snapID, false);
    }

    void setSnapEnabled (te::Edit& edit, bool on)
    {
        edit.state.getOrCreateChildWithName (ViewIDs::CDAWVIEW, nullptr).setProperty (snapID, on, nullptr);
    }
}
