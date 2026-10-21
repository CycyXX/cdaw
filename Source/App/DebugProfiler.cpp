#include "DebugProfiler.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
 #include <dbghelp.h>
 #include <thread>
 #include <map>
 #pragma comment (lib, "dbghelp.lib")
 #pragma comment (lib, "winmm.lib")
#endif

namespace cdaw::DebugProfiler
{
   #if JUCE_WINDOWS
    namespace
    {
        constexpr int maxFrames = 48;
        constexpr int maxSamples = 200000;

        struct Sample
        {
            DWORD64 pcs[maxFrames];
            int numFrames = 0;
        };

        /** Unwinds a suspended thread's stack without allocating (the thread may hold the heap lock). */
        int unwind (CONTEXT ctx, DWORD64* pcs)
        {
            int n = 0;

            while (n < maxFrames && ctx.Rip != 0)
            {
                pcs[n++] = ctx.Rip;

                DWORD64 imageBase = 0;
                auto* fn = RtlLookupFunctionEntry (ctx.Rip, &imageBase, nullptr);

                if (fn == nullptr)
                {
                    // Leaf function: the return address is on top of the stack.
                    ctx.Rip = *reinterpret_cast<DWORD64*> (ctx.Rsp);
                    ctx.Rsp += 8;
                    continue;
                }

                void* handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
            }

            return n;
        }

        juce::String nameFor (HANDLE process, DWORD64 address, std::map<DWORD64, juce::String>& cache)
        {
            if (auto it = cache.find (address); it != cache.end())
                return it->second;

            alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 512] = {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
            symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
            symbol->MaxNameLen = 511;

            DWORD64 displacement = 0;
            juce::String name = SymFromAddr (process, address, &displacement, symbol)
                                    ? juce::String (symbol->Name)
                                    : juce::String::toHexString ((juce::int64) address);
            cache[address] = name;
            return name;
        }

        void report (const std::vector<Sample>& samples, double seconds)
        {
            auto process = GetCurrentProcess();
            SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize (process, nullptr, TRUE);

            std::map<DWORD64, juce::String> names;
            std::map<juce::String, int> self, inclusive;

            for (auto& s : samples)
            {
                if (s.numFrames == 0)
                    continue;

                ++self[nameFor (process, s.pcs[0], names)];

                juce::StringArray seen;
                for (int i = 0; i < s.numFrames; ++i)
                {
                    auto n = nameFor (process, s.pcs[i], names);
                    if (! seen.contains (n))
                    {
                        seen.add (n);
                        ++inclusive[n];
                    }
                }
            }

            SymCleanup (process);

            auto top = [&] (const std::map<juce::String, int>& counts, const char* title, int limit)
            {
                std::vector<std::pair<int, juce::String>> sorted;
                for (auto& [n, c] : counts)
                    sorted.push_back ({ c, n });
                std::sort (sorted.rbegin(), sorted.rend());

                juce::String text = "PROFILE " + juce::String (title) + " (" + juce::String ((int) samples.size()) + " samples over "
                                    + juce::String (seconds, 1) + " s):\n";
                for (int i = 0; i < juce::jmin (limit, (int) sorted.size()); ++i)
                    text << "  " << juce::String (100.0 * sorted[(size_t) i].first / juce::jmax<size_t> (1, samples.size()), 1)
                         << "%  " << sorted[(size_t) i].second.substring (0, 140) << "\n";

                juce::Logger::writeToLog (text);
            };

            top (self, "self time", 30);
            top (inclusive, "inclusive time", 60);
        }
    }

    /** The thread of this process (other than the caller) that used the most CPU over `ms`. */
    static HANDLE findHottestThread (DWORD exclude, int ms)
    {
        auto snapshotTimes = []
        {
            std::map<DWORD, ULONGLONG> times;
            auto snap = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te { sizeof (te) };

            for (BOOL ok = Thread32First (snap, &te); ok; ok = Thread32Next (snap, &te))
            {
                if (te.th32OwnerProcessID != GetCurrentProcessId())
                    continue;

                if (auto h = OpenThread (THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID))
                {
                    FILETIME c, e, k, u;
                    if (GetThreadTimes (h, &c, &e, &k, &u))
                        times[te.th32ThreadID] = (((ULONGLONG) u.dwHighDateTime << 32) | u.dwLowDateTime)
                                               + (((ULONGLONG) k.dwHighDateTime << 32) | k.dwLowDateTime);
                    CloseHandle (h);
                }
            }

            CloseHandle (snap);
            return times;
        };

        auto before = snapshotTimes();
        Sleep ((DWORD) ms);
        auto after = snapshotTimes();

        DWORD best = 0;
        ULONGLONG bestDelta = 0;

        for (auto& [id, t] : after)
            if (id != exclude && before.count (id) && t - before[id] > bestDelta)
            {
                bestDelta = t - before[id];
                best = id;
            }

        juce::Logger::writeToLog ("PROFILE hottest thread " + juce::String ((int) best) + ": "
                                  + juce::String (bestDelta / 10000.0 / ms * 100.0, 1) + "% of a core");
        return best != 0 ? OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, best) : nullptr;
    }

    void start (double seconds, bool hottestThread)
    {
        HANDLE target = nullptr;

        if (! hottestThread)
            DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target,
                             THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);

        std::thread ([target, seconds, hottestThread]() mutable
        {
            if (hottestThread)
                target = findHottestThread (GetCurrentThreadId(), 2000);

            if (target == nullptr)
                return;

            // Allocate everything up front: nothing may allocate while the target is suspended.
            auto samples = std::make_unique<std::vector<Sample>>();
            samples->resize ((size_t) maxSamples);
            size_t count = 0;

            timeBeginPeriod (1);
            const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;

            while (juce::Time::getMillisecondCounterHiRes() < end && count < (size_t) maxSamples)
            {
                Sleep (1);

                if (SuspendThread (target) == (DWORD) -1)
                    break;

                CONTEXT ctx {};
                ctx.ContextFlags = CONTEXT_FULL;

                if (GetThreadContext (target, &ctx))
                {
                    auto& s = (*samples)[count++];
                    s.numFrames = unwind (ctx, s.pcs);
                }

                ResumeThread (target);
            }

            timeEndPeriod (1);
            CloseHandle (target);
            samples->resize (count);
            report (*samples, seconds);
        }).detach();
    }
   #else
    void start (double, bool) {}
   #endif
}
