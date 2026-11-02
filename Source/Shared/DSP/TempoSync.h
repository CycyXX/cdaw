#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace cdaw::TempoSync
{
    /** Note lengths for tempo-synced delay times and LFO rates, shortest first (so a knob sweeps
        them in order). Stored by index in projects and presets: append only. */
    struct Division { const char* name; double beats; };

    inline constexpr std::array<Division, 18> divisions {{
        { "1/64", 0.0625 }, { "1/32", 0.125 }, { "1/16 T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8 T", 1.0 / 3.0 },
        { "1/16 D", 0.375 }, { "1/8", 0.5 }, { "1/4 T", 2.0 / 3.0 }, { "1/8 D", 0.75 }, { "1/4", 1.0 },
        { "1/2 T", 4.0 / 3.0 }, { "1/4 D", 1.5 }, { "1/2", 2.0 }, { "1/1 T", 8.0 / 3.0 }, { "1/2 D", 3.0 },
        { "1/1", 4.0 }, { "2 bars", 8.0 }, { "4 bars", 16.0 }
    }};

    /** Delays use the divisions up to a whole note. */
    inline constexpr int numDelayDivisions = 16;

    inline const Division& get (int index)
    {
        return divisions[(size_t) juce::jlimit (0, (int) divisions.size() - 1, index)];
    }

    inline juce::StringArray getNames (int count = (int) divisions.size())
    {
        juce::StringArray names;
        for (int i = 0; i < count; ++i)
            names.add (divisions[(size_t) i].name);
        return names;
    }

    inline double toSeconds (int index, double bpm)
    {
        return get (index).beats * 60.0 / juce::jlimit (20.0, 999.0, bpm);
    }

    /** One LFO cycle per division. */
    inline double toHz (int index, double bpm)
    {
        return 1.0 / toSeconds (index, bpm);
    }

    /** Where the host's song is at the start of a block. */
    struct SongPosition
    {
        bool playing = false;
        double seconds = 0.0, beats = 0.0;
    };

    /** Keeps an LFO in step with the song, so playback and a render of it sound alike whatever
        the host did before: a synced LFO follows the beats exactly; a free one takes its phase
        from the song position when playback starts or jumps, then runs on (so turning its rate
        never makes it jump). */
    struct LfoClock
    {
        /** True when a free LFO should take its phase from the song now (call once per block). */
        bool shouldLock (const SongPosition& position, int numSamples, double sampleRate)
        {
            const bool lock = position.playing && (! wasPlaying || std::abs (position.seconds - expectedSeconds) > 0.002);
            wasPlaying = position.playing;
            expectedSeconds = position.seconds + numSamples / sampleRate;
            return lock;
        }

        /** The phase (cycles) at the song position for a free LFO at `hz`, or a synced one of `division`. */
        static double phaseAt (const SongPosition& position, double hz)                  { return wrap (hz * position.seconds); }
        static double syncedPhaseAt (const SongPosition& position, int division)         { return wrap (position.beats / get (division).beats); }
        static double wrap (double phase)                                                { return phase - std::floor (phase); }

        bool wasPlaying = false;
        double expectedSeconds = 0.0;
    };

    /** The division (among the first `count`) closest to a length in seconds, on a log scale. */
    inline int nearest (double seconds, double bpm, int count = (int) divisions.size())
    {
        int best = 0;
        double bestDistance = 1.0e9;

        for (int i = 0; i < count; ++i)
        {
            auto d = std::abs (std::log (juce::jmax (1.0e-6, seconds) / toSeconds (i, bpm)));
            if (d < bestDistance)
            {
                best = i;
                bestDistance = d;
            }
        }

        return best;
    }
}
