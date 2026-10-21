#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Offline rendering: mixdown, stems and MIDI export. */
    namespace ExportService
    {
        enum class Format { wav, flac, ogg, mp3 };
        enum class Range { song, loop };
        enum class Normalise { none, peak, lufs };

        struct Options
        {
            Format format = Format::wav;
            int bitDepth = 24;                 // 16, 24, 32 (float) for WAV; 16/24 FLAC
            double sampleRate = 48000.0;
            Range range = Range::song;
            double tailSeconds = 2.0;          // let reverbs/delays ring out
            Normalise normalise = Normalise::none;
            float normaliseLevel = -1.0f;      // dBFS for peak, LUFS for loudness
            float truePeakCeiling = -1.0f;
            bool dither = true;
            int mp3QualityIndex = -1;          // -1 = best (320 kbps)
        };

        juce::String getExtension (Format);
        bool isFormatAvailable (te::Engine&, Format);
        te::TimeRange getRenderRange (te::Edit&, const Options&);

        /** Builds render parameters for the whole mix (tracks empty) or specific tracks (stems). */
        te::Renderer::Parameters makeParameters (te::Edit&, const Options&, const juce::File& dest,
                                                 const juce::Array<te::Track*>& tracks);

        /** Writes selected MIDI clips (or all MIDI clips) to a type-1 MIDI file with the tempo map. */
        bool exportMidi (te::Edit&, const juce::Array<te::MidiClip*>&, const juce::File& dest);

        /** Runs a list of renders one after another on background threads. */
        class Job : private juce::Timer
        {
        public:
            struct Item
            {
                te::Renderer::Parameters params;
                juce::String description;
            };

            Job (std::vector<Item>, std::function<void (bool ok, juce::StringArray results)> onFinished);
            ~Job() override;

            void cancel();
            float getProgress() const           { return progress; }
            juce::String getCurrentDescription() const;
            bool isFinished() const             { return finished; }

        private:
            void startNext();
            void timerCallback() override;

            std::vector<Item> items;
            size_t current = 0;
            std::shared_ptr<te::EditRenderer::Handle> handle;
            std::function<void (bool, juce::StringArray)> onFinished;
            juce::StringArray results;
            float progress = 0.0f;
            bool finished = false, failed = false, cancelled = false;
            std::unique_ptr<te::Edit::SaveInhibitor> saveInhibitor;
            std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        };
    }
}
