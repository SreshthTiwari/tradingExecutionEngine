#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#include "market_data/replay.hpp"

int main(int argc, char **argv)
{
    const char *path = nullptr;
    bool print_trades = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--print-trades") == 0)
            print_trades = true;
        else
            path = argv[i];
    }
    if (!path)
    {
        std::cerr << "usage: replay <commands.txt> [--print-trades]\n";
        return 2;
    }

    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "cannot open " << path << "\n";
        return 1;
    }

    MatchingEngine engine;
    const auto start = std::chrono::steady_clock::now();
    const ReplayStats stats = replay_stream(file, engine, [&](const Trade &t) {
        if (print_trades)
            std::cout << "TRADE " << t.taker_id << " " << t.maker_id << " " << t.price << " " << t.quantity << "\n";
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::cout << "commands:  " << stats.commands << " (" << stats.rejected << " rejected, "
              << stats.malformed << " malformed lines skipped)\n"
              << "trades:    " << stats.trades << "\n"
              << "volume:    " << stats.volume << "\n";
    if (stats.volume > 0)
        std::cout << "vwap:      " << static_cast<double>(stats.notional / stats.volume) << " ticks\n"
                  << "last:      " << stats.last_trade_price << " ticks\n";

    const OrderBook &book = engine.book();
    std::cout << "resting:   " << book.order_count() << " orders\n";
    if (const auto bid = book.best_bid())
        std::cout << "best bid:  " << *bid << " (qty " << book.depth_at(Side::Buy, *bid) << ")\n";
    if (const auto ask = book.best_ask())
        std::cout << "best ask:  " << *ask << " (qty " << book.depth_at(Side::Sell, *ask) << ")\n";
    std::cout << "replayed in " << seconds * 1000 << " ms\n";
    return 0;
}
