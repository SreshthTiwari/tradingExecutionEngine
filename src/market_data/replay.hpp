#ifndef REPLAY_HPP
#define REPLAY_HPP

#include <cstddef>
#include <functional>
#include <istream>

#include "core/matching_engine.hpp"

struct ReplayStats
{
    std::size_t lines = 0;     // non-blank, non-comment lines read
    std::size_t malformed = 0; // lines that failed to parse (skipped)
    std::size_t commands = 0;  // commands applied to the engine
    std::size_t rejected = 0;  // commands the engine refused (any non-Ok status)
    std::size_t trades = 0;
    Quantity volume = 0;       // total traded quantity
    Price last_trade_price = 0;
    long double notional = 0;  // sum of price * quantity over all trades
};

// Feeds a command stream (the text format from core/command.hpp, '#' comments allowed)
// through the engine. `on_trade`, if set, is called for every trade in order.
ReplayStats replay_stream(std::istream &in, MatchingEngine &engine,
                          const std::function<void(const Trade &)> &on_trade = {});

#endif // REPLAY_HPP
