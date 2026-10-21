#include "AnalyserFifo.h"

namespace cdaw
{
    void AnalyserFifo::push (const juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (! active.load() || numChannels == 0 || numSamples == 0)
            return;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        const float scale = 1.0f / (float) numChannels;
        int read = 0;

        auto write = [&] (int dest, int count)
        {
            for (int i = 0; i < count; ++i, ++read)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sum += buffer.getSample (ch, read);

                data[(size_t) (dest + i)] = sum * scale;
            }
        };

        write (start1, size1);
        write (start2, size2);
        fifo.finishedWrite (size1 + size2);
    }

    int AnalyserFifo::pull (float* dest, int maxSamples)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (juce::jmin (maxSamples, fifo.getNumReady()), start1, size1, start2, size2);

        std::copy_n (data.data() + start1, size1, dest);
        std::copy_n (data.data() + start2, size2, dest + size1);
        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }
}
