#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <optional>
#include <string>
#include <vector>

#include "matching_engine.hpp"

// One line of the engine's text protocol. The same format is used by the TCP gateway and
// by replay files:
//
//   NEW <id> <B|S> <price> <qty> [GTC|IOC|FOK]   limit order (default GTC)
//   MKT <id> <B|S> <qty> [IOC|FOK]               market order (default IOC)
//   CANCEL <id>
//   MODIFY <id> <price> <qty>                    new price and total remaining quantity
//
// keywords are case-insensitive; prices are integer ticks
struct Command
{
    enum class Kind
    {
        New,
        Cancel,
        Modify
    };

    Kind kind;
    OrderRequest order; // New: whole request. Cancel: only id. Modify: id, price, quantity
};

// returns std::nullopt for a blank, comment (#) or malformed line
std::optional<Command> parse_command(const std::string &line);

// runs a command against the engine, appending any trades. A cancel returns Status::Ok
// or Status::NotFound.
Status apply_command(MatchingEngine &engine, const Command &command, std::vector<Trade> &trades);

#endif // COMMAND_HPP
