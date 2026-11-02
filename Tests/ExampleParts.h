#pragma once

// What the generators of the example projects share: synthesised drums, a few sounds, and renders
// to measure the mix with.

#include "TestEngine.h"
#include "Plugins/LeadPlugin.h"
#include "Plugins/SynthPlugin.h"
#include "Shared/Lead/LeadPresets.h"
#include "Shared/Synth/SynthPresets.h"

namespace cdaw::test::example
{
    //==============================================================================
    // Drum synthesis: each hit is added into a stereo buffer at a time in beats.

    struct DrumKit
    {
        DrumKit (int numSamples, std::function<double (double beat)> beatToSeconds, double sampleRateToUse, juce::int64 seed = 2026)
            : kick (2, numSamples), snare (2, numSamples), hats (2, numSamples),
              toSeconds (std::move (beatToSeconds)), sampleRate (sampleRateToUse), random (seed)
        {
            for (auto* b : { &kick, &snare, &hats })
                b->clear();
        }

        int at (double beat) const     { return (int) std::round (toSeconds (beat) * sampleRate); }

        void addKick (double beat, float gain)
        {
            // A sine dropping from 150 Hz to 48 Hz, with a short click on top.
            const int start = at (beat), length = (int) (0.45 * sampleRate);
            double phase = 0.0;
            for (int i = 0; i < length && start + i < kick.getNumSamples(); ++i)
            {
                const double t = i / sampleRate;
                const double freq = 48.0 + 102.0 * std::exp (-t * 28.0);
                phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
                const double body = std::sin (phase) * std::exp (-t * 7.5);
                const double click = (i < 96 ? random.nextFloat() * 2.0 - 1.0 : 0.0) * 0.25 * std::exp (-t * 400.0);
                const auto v = (float) ((body + click) * gain);
                kick.addSample (0, start + i, v);
                kick.addSample (1, start + i, v);
            }
        }

        void addSnare (double beat, float gain, float pan = 0.0f)
        {
            // A 190 Hz tone and band-limited noise, decaying fast; the noise slightly wider.
            const int start = at (beat), length = (int) (0.32 * sampleRate);
            float lowL = 0, lowR = 0, prevL = 0, prevR = 0;
            for (int i = 0; i < length && start + i < snare.getNumSamples(); ++i)
            {
                const double t = i / sampleRate;
                const double tone = std::sin (juce::MathConstants<double>::twoPi * 190.0 * t) * std::exp (-t * 30.0) * 0.6;
                auto noise = [&] (float& low, float& prev)
                {
                    const float n = random.nextFloat() * 2.0f - 1.0f;
                    low += 0.35f * (n - low);                   // gentle low-pass
                    const float hp = low - prev;                // then remove the lows
                    prev = low;
                    return hp * 2.2f * (float) std::exp (-t * 14.0);
                };
                const float nl = noise (lowL, prevL), nr = noise (lowR, prevR);
                snare.addSample (0, start + i, (float) (tone + nl) * gain * (1.0f - pan));
                snare.addSample (1, start + i, (float) (tone + nr) * gain * (1.0f + pan));
            }
        }

        void addHat (double beat, float gain, bool open, float pan)
        {
            const int start = at (beat), length = (int) ((open ? 0.35 : 0.06) * sampleRate);
            const double decay = open ? 9.0 : 70.0;
            float prev = 0;
            for (int i = 0; i < length && start + i < hats.getNumSamples(); ++i)
            {
                const double t = i / sampleRate;
                const float n = random.nextFloat() * 2.0f - 1.0f;
                const float hp = n - prev * 0.92f;              // bright: mostly the top
                prev = n;
                const float v = hp * gain * 0.5f * (float) std::exp (-t * decay);
                hats.addSample (0, start + i, v * (1.0f - pan));
                hats.addSample (1, start + i, v * (1.0f + pan));
            }
        }

        juce::AudioBuffer<float> kick, snare, hats;
        std::function<double (double)> toSeconds;
        double sampleRate;
        juce::Random random;
    };

    inline void writeWav (juce::AudioBuffer<float>& buffer, const juce::File& file, double sampleRate = 48000.0)
    {
        // Each stem peaks at -1 dBFS; the faders do the balancing.
        if (auto peak = buffer.getMagnitude (0, buffer.getNumSamples()); peak > 0.0f)
            buffer.applyGain (juce::Decibels::decibelsToGain (-1.0f) / peak);

        file.deleteFile();
        juce::WavAudioFormat wav;
        if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), sampleRate, (unsigned int) buffer.getNumChannels(), 24, {}, 0)))
            {
                out.release();
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }
    }

    //==============================================================================
    inline te::FourOscPlugin* addSynth (te::AudioTrack& track)
    {
        for (auto p : track.pluginList.getPlugins())
            if (auto synth = dynamic_cast<te::FourOscPlugin*> (p))
                return synth;
        return nullptr;
    }

    inline void set (te::AutomatableParameter::Ptr p, float v)     { p->setParameter (v, juce::sendNotification); }

    inline void soundBass (te::FourOscPlugin& s)
    {
        s.oscParams[0]->waveShapeValue = 3;                 // saw
        s.oscParams[1]->waveShapeValue = 2;                 // square, an octave down
        set (s.oscParams[1]->tune, -12.0f);
        set (s.oscParams[1]->level, -6.0f);
        s.filterTypeValue = 1;                              // low-pass
        set (s.filterFreq, 52.0f);
        set (s.filterResonance, 18.0f);
        set (s.filterAmount, 0.35f);
        set (s.filterAttack, 0.0f);
        set (s.filterDecay, 0.22f);
        set (s.filterSustain, 20.0f);
        set (s.ampAttack, 0.003f);
        set (s.ampDecay, 0.3f);
        set (s.ampSustain, 75.0f);
        set (s.ampRelease, 0.07f);
        set (s.masterLevel, -6.0f);
    }

    /** Puts one of a shared plugin's presets on it: its values, name and macro names. */
    template <typename Preset>
    void applyPreset (SharedEffectPlugin& plugin, const Preset& preset, const std::vector<float>& values)
    {
        for (int i = 0; i < (int) values.size(); ++i)
            plugin.parameter (i).setParameter (values[(size_t) i], juce::sendNotification);

        plugin.state.setProperty ("presetName", preset.name, nullptr);
        for (int m = 0; m < preset.macroNames.size(); ++m)
            plugin.state.setProperty (juce::Identifier ("macroName" + juce::String (m + 1)), preset.macroNames[m], nullptr);
    }

    template <typename Preset>
    const Preset& findPreset (const std::vector<Preset>& presets, const juce::String& name)
    {
        for (auto& p : presets)
            if (p.name == name)
                return p;
        jassertfalse;
        return presets.front();
    }

    /** Night Drive v2's pad: C-Synth's Warm Horizon, as it ships. */
    inline void soundPad (SynthPlugin& s)
    {
        const auto& preset = findPreset (SynthPresets::factory(), "Warm Horizon");
        applyPreset (s, preset, SynthPresets::valuesOf (preset));
    }

    /** Night Drive v2's lead: C-Lead's Supersaw Anthem, its chorus and dotted-eighth ping-pong
        kept, its own reverb off (the song's reverbs give the lead its space). */
    inline void soundLead (LeadPlugin& s)
    {
        const auto& preset = findPreset (LeadPresets::factory(), "Supersaw Anthem");
        auto values = LeadPresets::valuesOf (preset);
        values[(size_t) LeadParameters::reverbOn] = 0.0f;
        applyPreset (s, preset, values);
    }

    //==============================================================================
    /** Renders the edit (only `tracks` and their parent groups when given) at 48 kHz in 32-bit
        float, so levels above 0 dBFS come back as they are. */
    inline juce::AudioBuffer<float> render (te::Edit& edit, juce::Array<te::Track*> tracks = {})
    {
        for (int i = tracks.size(); --i >= 0;)
            if (auto parent = tracks[i]->getParentFolderTrack())
                tracks.addIfNotAlreadyThere (parent);

        juce::TemporaryFile temp (".wav");
        te::Renderer::Parameters params (edit);
        params.destFile = temp.getFile();
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth = 32;
        params.sampleRateForAudio = 48000.0;
        params.time = te::TimeRange (te::TimePosition(), edit.getLength());
        params.tracksToDo = te::toBitSet (tracks.isEmpty() ? te::getAllTracks (edit) : tracks);
        params.useMasterPlugins = tracks.isEmpty();     // the full mix through the master chain; parts before it
        te::Renderer::renderToFile ("Example song", params);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::AudioBuffer<float> buffer;
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (temp.getFile())))
        {
            buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        }
        return buffer;
    }

    inline float peakDb (const juce::AudioBuffer<float>& b)    { return juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -150.0f); }

    inline float rmsDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec, double rate)
    {
        auto s = (int) (fromSec * rate), e = juce::jmin (b.getNumSamples(), (int) (toSec * rate));
        float sum = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            sum += b.getRMSLevel (ch, s, e - s);
        return juce::Decibels::gainToDecibels (sum / (float) b.getNumChannels(), -150.0f);
    }
}
