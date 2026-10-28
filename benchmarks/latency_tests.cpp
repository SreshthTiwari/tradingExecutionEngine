#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#include "workload.hpp"

// smallest step the clock can report; latencies below this read as 0 or one step
static long long timer_granularity_ns()
{
    long long smallest = 1'000'000;
    for (int i = 0; i < 100000; ++i)
    {
        const auto a = std::chrono::steady_clock::now();
        auto b = std::chrono::steady_clock::now();
        while (b == a)
            b = std::chrono::steady_clock::now();
        smallest = std::min<long long>(smallest, std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    }
    return smallest;
}

int main()
{
    const std::size_t warmup = 200'000;
    const std::size_t count = 1'000'000;
    const auto msgs = make_workload(warmup + count);

    MatchingEngine engine;
    std::size_t trades = 0;
    std::vector<Trade> buffer;
    for (std::size_t i = 0; i < warmup; ++i)
        apply(engine, msgs[i], trades, buffer);

    // per-message latency; includes ~tens of ns of steady_clock overhead
    std::vector<long long> ns;
    ns.reserve(count);
    for (std::size_t i = warmup; i < warmup + count; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        apply(engine, msgs[i], trades, buffer);
        const auto t1 = std::chrono::steady_clock::now();
        ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::sort(ns.begin(), ns.end());
    auto pct = [&](double p)
    { return ns[static_cast<std::size_t>(p * (ns.size() - 1))]; };

    std::cout << "messages measured: " << count << " (after " << warmup << " warmup)\n"
              << "timer granularity: ~" << timer_granularity_ns() << " ns (percentiles are quantized to this)\n"
              << "p50:   " << pct(0.50) << " ns\n"
              << "p90:   " << pct(0.90) << " ns\n"
              << "p99:   " << pct(0.99) << " ns\n"
              << "p99.9: " << pct(0.999) << " ns\n"
              << "max:   " << ns.back() << " ns\n";
    return 0;
}
