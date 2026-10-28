#ifndef TRADE_EXECUTION_HPP
#define TRADE_EXECUTION_HPP

#include <optional>
#include <vector>

#include "matching_engine.hpp"
#include "risk_checks.hpp"

// gates orders through pre-trade risk checks before they reach the matching engine
class TradeExecution
{
public:
    explicit TradeExecution(RiskLimits limits, Price max_price = OrderBook::kDefaultMaxPrice)
        : limits_(limits), engine_(max_price) {}

    // risk-checks then matches an order; trades are appended to `trades`.
    // market orders are checked for size only (they have no price)
    Status submit(const OrderRequest &request, std::vector<Trade> &trades);

    // risk-checks the new price/quantity, then modifies the resting order
    Status modify(OrderId id, Price new_price, Quantity new_quantity, std::vector<Trade> &trades);

    bool cancel(OrderId id) { return engine_.cancel_order(id); }

    // GTC limit order; returns std::nullopt if rejected for any reason,
    // otherwise the trades it produced (possibly empty if it only rested)
    std::optional<std::vector<Trade>> execute(OrderId id, Side side, Price price, Quantity quantity);

    const MatchingEngine &engine() const { return engine_; }

private:
    RiskLimits limits_;
    MatchingEngine engine_;
};

#endif // TRADE_EXECUTION_HPP
