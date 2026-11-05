#include "TestEngine.h"
#include "App/DebugProfiler.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

static std::atomic<juce::uint32> lastProgressMs { 0 };

struct ConsoleRunner : public juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        lastProgressMs = juce::Time::getMillisecondCounter();
        std::cout << message << std::endl;
    }
};

/** With CDAW_TEST_HANG_SECONDS set, as on CI: a test that stops making progress ends the run and
    says where the main thread is stuck, rather than using up the job's time limit in silence. */
struct HangGuard
{
    HangGuard()
    {
        const auto limitMs = (juce::uint32) juce::SystemStats::getEnvironmentVariable ("CDAW_TEST_HANG_SECONDS", {}).getIntValue() * 1000;

        if (limitMs == 0)
            return;

        mainThread = cdaw::DebugProfiler::currentThreadHandle();
        lastProgressMs = juce::Time::getMillisecondCounter();

        watcher = std::thread ([this, limitMs]
        {
            while (! stop)
            {
                std::this_thread::sleep_for (std::chrono::seconds (1));

                if (juce::Time::getMillisecondCounter() - lastProgressMs > limitMs)
                {
                    std::cout << std::endl << "*** HUNG *** no progress for " << limitMs / 1000 << " s. The main thread is at:" << std::endl
                              << cdaw::DebugProfiler::captureStack (mainThread) << std::endl;
                    std::_Exit (3);
                }
            }
        });
    }

    ~HangGuard()
    {
        if (watcher.joinable())
        {
            stop = true;
            watcher.join();
            cdaw::DebugProfiler::releaseThreadHandle (mainThread);
        }
    }

    void* mainThread = nullptr;
    std::atomic<bool> stop { false };
    std::thread watcher;
};

static void crashHandler (void*)
{
    std::cout << std::endl << "*** CRASH ***" << std::endl << juce::SystemStats::getStackBacktrace() << std::endl;
}

int main (int argc, char* argv[])
{
    juce::SystemStats::setApplicationCrashHandler (crashHandler);
    int failures = 0, passes = 0, results = 0;

    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        HangGuard hangGuard;

        ConsoleRunner runner;
        runner.setAssertOnFailure (false);
        // The argument is a category, or the name of a single test.
        const auto which = argc > 1 ? juce::String (argv[1]) : juce::String ("CDAW");
        juce::Array<juce::UnitTest*> named;
        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getName() == which)
                named.add (test);

        if (named.isEmpty())
            runner.runTestsInCategory (which);
        else
            runner.runTests (named);

        results = runner.getNumResults();
        for (int i = 0; i < results; ++i)
        {
            failures += runner.getResult (i)->failures;
            passes += runner.getResult (i)->passes;
        }

        cdaw::test::destroyEngine();
    }

    std::cout << std::endl << "CDAW tests: " << passes << " passed, " << failures << " failed" << std::endl;
    return failures == 0 && results > 0 ? 0 : 1;
}
