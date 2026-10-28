#ifndef MARKET_DATA_INTERFACE_HPP
#define MARKET_DATA_INTERFACE_HPP

#include <optional>
#include <string>

// fetches the latest price for a ticker by running yahoo_data.py (requires python3 + yfinance).
// returns std::nullopt if the symbol is invalid or the lookup fails
std::optional<double> get_stock_price(const std::string &symbol);

#endif // MARKET_DATA_INTERFACE_HPP
