#include <chrono>
#include <iostream>

#include "workload.hpp"

int main()
{
    const std::size_t count = 2'000'000;
    const auto msgs = make_workload(count);

    MatchingEngine engine;
    std::size_t trades = 0;
    std::vector<Trade> buffer;

    const auto start = std::chrono::steady_clock::now();
    for (const Message &m : msgs)
        apply(engine, m, trades, buffer);
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    std::cout << "messages:   " << count << "\n"
              << "trades:     " << trades << "\n"
              << "resting:    " << engine.book().order_count() << "\n"
              << "time:       " << seconds << " s\n"
              << "throughput: " << static_cast<long long>(count / seconds) << " msgs/sec\n";
    return 0;
}
