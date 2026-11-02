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

        /** dbfs: -60 dB to 0 dBFS fills the bar (input meters). fader: the bar shares the mixer
            fader's dB scale, +6 dB at the top and silence at the bottom, so a vertical meter
            sized to the fader's travel lines up with the fader's marks. */
        enum class Scale { dbfs, fader };
        void setScale (Scale);

        /** Levels above this light the clip LED. 0 dBFS (the default) is real clipping at an
            output or an input; a track meter in fader scale can go up to the fader's +6 dB. */
        void setClipThresholdDb (float db)      { clipThresholdDb = db; }

        /** The loudest peak (any channel) since the last reset; -100 if none. */
        float getHeldPeakDb() const noexcept    { return heldPeak; }
        bool hasClipped() const noexcept        { return clipped; }
        void resetPeaks();

        /** Height of the clip LED strip at the top of a vertical meter. */
        static constexpr float clipLedHeight = 4.0f;

    private:
        float toProportion (float db) const;

        void timerCallback() override;

        te::LevelMeasurer* measurer = nullptr;
        te::Plugin::Ptr plugin;
        te::LevelMeasurer::Client client;

        static constexpr int maxChannels = 2;
        float level[maxChannels] { -100.0f, -100.0f };
        float peak[maxChannels] { -100.0f, -100.0f };
        juce::uint32 peakTime[maxChannels] {};
        float heldPeak = -100.0f;
        float clipThresholdDb = 0.0f;
        Scale scale = Scale::dbfs;
        bool clipped = false;
        int numChannels = 2;
    };
}
