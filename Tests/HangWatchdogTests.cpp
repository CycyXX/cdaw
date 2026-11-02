#include "TestEngine.h"
#include "App/DebugProfiler.h"
#include <thread>

namespace cdaw::test
{
    struct HangWatchdogTests : public juce::UnitTest
    {
        HangWatchdogTests() : juce::UnitTest ("Hang watchdog", "CDAW") {}

       #if JUCE_MSVC
        __declspec (noinline)
       #endif
        static void waitHere()
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (500));
        }

        void runTest() override
        {
            beginTest ("Another thread can see where this thread is stuck, by module and function");
            {
                auto self = DebugProfiler::currentThreadHandle();
                juce::String stack;

                std::thread watcher ([&]
                {
                    std::this_thread::sleep_for (std::chrono::milliseconds (150));
                    stack = DebugProfiler::captureStack (self);
                });

                waitHere();
                watcher.join();
                DebugProfiler::releaseThreadHandle (self);

               #if JUCE_WINDOWS
                logMessage (stack.upToFirstOccurrenceOf ("\n", false, false));
                expect (stack.contains ("CDAW_Tests.exe!"), "names the module");
                expect (stack.contains ("waitHere"), "names the function it's waiting in");
               #endif
            }
        }
    };

    static HangWatchdogTests hangWatchdogTests;
}
