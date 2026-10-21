#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Peak meter with hold and clip indicator. Polls a te::LevelMeasurer at 30 Hz.
        Vertical when taller than wide, horizontal otherwise. Click to reset the clip LED. */
    class LevelMeter : public juce::Component,
                       private juce::Timer
    {
    public:
        LevelMeter();
        ~LevelMeter() override;

        /** Attaches to a measurer. keepAlive holds the owning plugin so the measurer outlives us. */
        void attach (te::LevelMeasurer*, te::Plugin::Ptr keepAlive = {});
        void detach();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        static float dbToProportion (float db);

    private:
        void timerCallback() override;

        te::LevelMeasurer* measurer = nullptr;
        te::Plugin::Ptr plugin;
        te::LevelMeasurer::Client client;

        static constexpr int maxChannels = 2;
        float level[maxChannels] { -100.0f, -100.0f };
        float peak[maxChannels] { -100.0f, -100.0f };
        juce::uint32 peakTime[maxChannels] {};
        bool clipped = false;
        int numChannels = 2;
    };
}
