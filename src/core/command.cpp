#include "command.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace
{
std::string upper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

// whole-token integer parse: rejects empty input, trailing junk and overflow
template <typename T>
bool parse_int(const std::string &token, T &out)
{
    const char *first = token.data();
    const char *last = first + token.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc() && result.ptr == last;
}

bool parse_side(const std::string &token, Side &side)
{
    const std::string t = upper(token);
    if (t == "B")
        side = Side::Buy;
    else if (t == "S")
        side = Side::Sell;
    else
        return false;
    return true;
}

bool parse_tif(const std::string &token, TimeInForce &tif)
{
    const std::string t = upper(token);
    if (t == "GTC")
        tif = TimeInForce::GTC;
    else if (t == "IOC")
        tif = TimeInForce::IOC;
    else if (t == "FOK")
        tif = TimeInForce::FOK;
    else
        return false;
    return true;
}
} // namespace

std::optional<Command> parse_command(const std::string &line)
{
    std::istringstream in(line);
    std::vector<std::string> t;
    for (std::string token; in >> token;)
        t.push_back(token);

    if (t.empty() || t[0][0] == '#')
        return std::nullopt;

    const std::string keyword = upper(t[0]);
    Command cmd{};

    if (keyword == "NEW" && (t.size() == 5 || t.size() == 6))
    {
        cmd.kind = Command::Kind::New;
        cmd.order.type = OrderType::Limit;
        cmd.order.tif = TimeInForce::GTC;
        if (!parse_int(t[1], cmd.order.id) || !parse_side(t[2], cmd.order.side) ||
            !parse_int(t[3], cmd.order.price) || !parse_int(t[4], cmd.order.quantity))
            return std::nullopt;
        if (t.size() == 6 && !parse_tif(t[5], cmd.order.tif))
            return std::nullopt;
        return cmd;
    }

    if (keyword == "MKT" && (t.size() == 4 || t.size() == 5))
    {
        cmd.kind = Command::Kind::New;
        cmd.order.type = OrderType::Market;
        cmd.order.tif = TimeInForce::IOC;
        cmd.order.price = 0;
        if (!parse_int(t[1], cmd.order.id) || !parse_side(t[2], cmd.order.side) ||
            !parse_int(t[3], cmd.order.quantity))
            return std::nullopt;
        if (t.size() == 5 && (!parse_tif(t[4], cmd.order.tif) || cmd.order.tif == TimeInForce::GTC))
            return std::nullopt;
        return cmd;
    }

    if (keyword == "CANCEL" && t.size() == 2)
    {
        cmd.kind = Command::Kind::Cancel;
        if (!parse_int(t[1], cmd.order.id))
            return std::nullopt;
        return cmd;
    }

    if (keyword == "MODIFY" && t.size() == 4)
    {
        cmd.kind = Command::Kind::Modify;
        if (!parse_int(t[1], cmd.order.id) || !parse_int(t[2], cmd.order.price) ||
            !parse_int(t[3], cmd.order.quantity))
            return std::nullopt;
        return cmd;
    }

    return std::nullopt;
}

Status apply_command(MatchingEngine &engine, const Command &command, std::vector<Trade> &trades)
{
    switch (command.kind)
    {
    case Command::Kind::New:
        return engine.submit(command.order, trades);
    case Command::Kind::Cancel:
        return engine.cancel_order(command.order.id) ? Status::Ok : Status::NotFound;
    case Command::Kind::Modify:
        return engine.modify_order(command.order.id, command.order.price, command.order.quantity, trades);
    }
    return Status::InvalidOrder;
}
