#pragma once

#include "../../PCH.h"
#include "../Common/FlaggedAsyncUpdater.h"

namespace cdaw
{
    /** List of a track's insert plugins: power, name (opens the editor), latency, remove,
        plus an empty slot to add one. Used by the inspector and the mixer strips. */
    class InsertSlots : public juce::Component,
                        private juce::ValueTree::Listener,
                        private FlaggedAsyncUpdater,
                        private juce::Timer
    {
    public:
        InsertSlots (te::Track::Ptr, bool compact);
        InsertSlots (te::PluginList&, bool compact);   // e.g. the master plugin list
        ~InsertSlots() override;

        void resized() override;
        void paint (juce::Graphics&) override;
        int getIdealHeight() const;

        static constexpr int rowHeight = 22;

    private:
        struct Row;

        void rebuild();
        void handleAsyncUpdate() override              { if (compareAndReset (needsRebuild)) rebuild(); }
        void timerCallback() override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override         { if (c.hasType (te::IDs::PLUGIN)) markAndUpdate (needsRebuild); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override  { if (c.hasType (te::IDs::PLUGIN)) markAndUpdate (needsRebuild); }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override            { markAndUpdate (needsRebuild); }
        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override
        {
            if (v.hasType (te::IDs::PLUGIN) && (id == te::IDs::enabled || id == te::IDs::name))
                markAndUpdate (needsRebuild);
        }

        te::Track::Ptr track;
        te::PluginList& list;
        bool compact;
        juce::OwnedArray<Row> rows;
        juce::TextButton addButton { "+ Insert" };
        bool needsRebuild = false;
    };

    /** List of a track's sends: bus, level, pre/post, remove, plus "+ Send". */
    class SendSlots : public juce::Component,
                      private juce::ValueTree::Listener,
                      private FlaggedAsyncUpdater
    {
    public:
        SendSlots (te::Track::Ptr, bool compact);
        ~SendSlots() override;

        void resized() override;
        int getIdealHeight() const;

    private:
        struct Row;

        void rebuild();
        void handleAsyncUpdate() override              { if (compareAndReset (needsRebuild)) rebuild(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override         { if (c.hasType (te::IDs::PLUGIN)) markAndUpdate (needsRebuild); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override  { if (c.hasType (te::IDs::PLUGIN)) markAndUpdate (needsRebuild); }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override            { markAndUpdate (needsRebuild); }

        te::Track::Ptr track;
        bool compact;
        juce::OwnedArray<Row> rows;
        juce::TextButton addButton { "+ Send" };
        bool needsRebuild = false;
    };
}
