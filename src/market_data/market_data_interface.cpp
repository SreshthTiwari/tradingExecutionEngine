#include "market_data_interface.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>

#ifndef MARKET_DATA_SCRIPT
#define MARKET_DATA_SCRIPT "src/market_data/yahoo_data.py"
#endif

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

// the symbol ends up in a shell command, so only allow ticker-like characters
static bool is_valid_symbol(const std::string &symbol)
{
    if (symbol.empty() || symbol.size() > 12)
        return false;
    for (unsigned char c : symbol)
    {
        if (!std::isalnum(c) && c != '.' && c != '-' && c != '^')
            return false;
    }
    return true;
}

std::optional<double> get_stock_price(const std::string &symbol)
{
    if (!is_valid_symbol(symbol))
        return std::nullopt;

    const std::string command = std::string("python3 \"") + MARKET_DATA_SCRIPT + "\" " + symbol + " 2>/dev/null";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe)
        return std::nullopt;

    std::string output;
    char buffer[128];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
        output += buffer;

    if (pclose(pipe) != 0)
        return std::nullopt;

    std::istringstream ss(output);
    double price;
    if (!(ss >> price))
        return std::nullopt;
    return price;
}
