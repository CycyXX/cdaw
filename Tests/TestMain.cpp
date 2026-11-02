#include "TestEngine.h"
#include <iostream>

struct ConsoleRunner : public juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
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
