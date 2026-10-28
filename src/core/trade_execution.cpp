#include "trade_execution.hpp"

Status TradeExecution::submit(const OrderRequest &request, std::vector<Trade> &trades)
{
    const bool passes = request.type == OrderType::Market
                            ? RiskChecks::check_order_size(request.quantity, limits_.max_order_size)
                            : RiskChecks::validate_order(request.quantity, request.price, limits_);
    if (!passes)
        return Status::RiskRejected;

    return engine_.submit(request, trades);
}

Status TradeExecution::modify(OrderId id, Price new_price, Quantity new_quantity, std::vector<Trade> &trades)
{
    if (!RiskChecks::validate_order(new_quantity, new_price, limits_))
        return Status::RiskRejected;

    return engine_.modify_order(id, new_price, new_quantity, trades);
}

std::optional<std::vector<Trade>> TradeExecution::execute(OrderId id, Side side, Price price, Quantity quantity)
{
    std::vector<Trade> trades;
    const Status status =
        submit(OrderRequest{id, side, OrderType::Limit, TimeInForce::GTC, price, quantity}, trades);
    if (status != Status::Ok)
        return std::nullopt;
    return trades;
}
