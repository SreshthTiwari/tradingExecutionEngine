#include "replay.hpp"

#include <string>
#include <vector>

#include "core/command.hpp"

ReplayStats replay_stream(std::istream &in, MatchingEngine &engine,
                          const std::function<void(const Trade &)> &on_trade)
{
    ReplayStats stats;
    std::vector<Trade> trades;
    std::string line;

    while (std::getline(in, line))
    {
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#')
            continue;
        ++stats.lines;

        const auto command = parse_command(line);
        if (!command)
        {
            ++stats.malformed;
            continue;
        }

        trades.clear();
        ++stats.commands;
        if (apply_command(engine, *command, trades) != Status::Ok)
            ++stats.rejected;

        for (const Trade &t : trades)
        {
            ++stats.trades;
            stats.volume += t.quantity;
            stats.notional += static_cast<long double>(t.price) * t.quantity;
            stats.last_trade_price = t.price;
            if (on_trade)
                on_trade(t);
        }
    }
    return stats;
}
