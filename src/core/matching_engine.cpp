#include "matching_engine.hpp"

Status MatchingEngine::submit(const OrderRequest &request, std::vector<Trade> &trades)
{
    const bool is_market = request.type == OrderType::Market;
    if (request.quantity <= 0 || request.id == kReservedOrderId)
        return Status::InvalidOrder;
    if (!is_market && (request.price < 1 || request.price > book_.max_price()))
        return Status::InvalidOrder;
    if (book_.contains(request.id))
        return Status::DuplicateId;

    const Side taker = request.side;
    const Side maker = opposite(taker);
    // a market order is a limit order that crosses everything
    const Price limit = !is_market ? request.price : (taker == Side::Buy ? book_.max_price() : 1);
    const TimeInForce tif = is_market && request.tif == TimeInForce::GTC ? TimeInForce::IOC : request.tif;

    if (tif == TimeInForce::FOK && book_.crossable_quantity(taker, limit, request.quantity) < request.quantity)
        return Status::NotFillable;

    Quantity remaining = request.quantity;
    while (remaining > 0)
    {
        const auto best = taker == Side::Buy ? book_.best_ask() : book_.best_bid();
        if (!best || (taker == Side::Buy ? *best > limit : *best < limit))
            break;

        const Order resting = book_.peek_best(maker);
        const Quantity fill = remaining < resting.quantity ? remaining : resting.quantity;
        trades.push_back(Trade{request.id, resting.id, resting.price, fill});
        book_.consume_best(maker, fill);
        remaining -= fill;
    }

    if (remaining > 0 && !is_market && tif == TimeInForce::GTC)
        book_.add_order(Order{request.id, taker, request.price, remaining});

    return Status::Ok;
}

Status MatchingEngine::modify_order(OrderId id, Price new_price, Quantity new_quantity, std::vector<Trade> &trades)
{
    const auto current = book_.find_order(id);
    if (!current)
        return Status::NotFound;
    if (new_quantity <= 0 || new_price < 1 || new_price > book_.max_price())
        return Status::InvalidOrder;

    if (new_price == current->price && new_quantity <= current->quantity)
    {
        if (new_quantity < current->quantity)
            book_.reduce_order(id, new_quantity);
        return Status::Ok;
    }

    book_.cancel_order(id);
    return submit(OrderRequest{id, current->side, OrderType::Limit, TimeInForce::GTC, new_price, new_quantity},
                  trades);
}

std::vector<Trade> MatchingEngine::submit_limit_order(OrderId id, Side side, Price price, Quantity quantity,
                                                      TimeInForce tif)
{
    std::vector<Trade> trades;
    submit(OrderRequest{id, side, OrderType::Limit, tif, price, quantity}, trades);
    return trades;
}

std::vector<Trade> MatchingEngine::submit_market_order(OrderId id, Side side, Quantity quantity)
{
    std::vector<Trade> trades;
    submit(OrderRequest{id, side, OrderType::Market, TimeInForce::IOC, 0, quantity}, trades);
    return trades;
}
