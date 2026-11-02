#include "HangWatchdog.h"
#include "DebugProfiler.h"

namespace cdaw
{
    namespace
    {
        std::atomic<double> lastSignOfLife { 0.0 };
        juce::CriticalSection activityLock;
        juce::String activity;

        double now()    { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    }

    HangWatchdog::HangWatchdog() : juce::Thread ("CDAW hang watchdog")
    {
        messageThread = DebugProfiler::currentThreadHandle();
        lastSignOfLife = now();
        startTimer (250);
        startThread (juce::Thread::Priority::low);
    }

    HangWatchdog::~HangWatchdog()
    {
        stopTimer();
        stopThread (2000);
        DebugProfiler::releaseThreadHandle (messageThread);
    }

    void HangWatchdog::setActivity (const juce::String& what)
    {
        {
            const juce::ScopedLock sl (activityLock);
            activity = what;
        }

        lastSignOfLife = now();
    }

    void HangWatchdog::timerCallback()
    {
        lastSignOfLife = now();
    }

    void HangWatchdog::run()
    {
        // Report a hang after 15 s, then at 45 s and 2 min; once more when it ends.
        constexpr double reportAt[] = { 15.0, 45.0, 120.0 };
        int reported = 0;
        double hangStart = 0.0;

        while (! threadShouldExit())
        {
            wait (500);

            const auto silent = now() - lastSignOfLife;

            if (silent < 2.0)
            {
                if (reported > 0)
                    juce::Logger::writeToLog ("Responding again after about " + juce::String (juce::roundToInt (now() - hangStart)) + " s");

                reported = 0;
                continue;
            }

            if (reported < (int) std::size (reportAt) && silent >= reportAt[reported])
            {
                if (reported == 0)
                    hangStart = lastSignOfLife;

                juce::String what;
                {
                    const juce::ScopedLock sl (activityLock);
                    what = activity;
                }

                juce::Logger::writeToLog ("*** Not responding for " + juce::String (juce::roundToInt (silent)) + " s"
                                          + (what.isNotEmpty() ? " (" + what + ")" : juce::String())
                                          + ". The main thread is in:\n" + DebugProfiler::captureStack (messageThread));
                ++reported;
            }
        }
    }
}
