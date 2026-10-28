#ifndef MATCHING_ENGINE_HPP
#define MATCHING_ENGINE_HPP

#include <vector>

#include "order_book.hpp"

class MatchingEngine
{
public:
    explicit MatchingEngine(Price max_price = OrderBook::kDefaultMaxPrice) : book_(max_price) {}

    // Matches an order against the book with price-time priority and appends any trades to
    // `trades` (it does not clear it, so callers can reuse one buffer).
    //  - limit orders match at their price or better; a GTC remainder rests on the book,
    //    an IOC remainder is dropped
    //  - a FOK order either fills completely or does nothing (Status::NotFillable)
    //  - market orders sweep the book at any price and never rest
    // Rejected orders (see Status) leave the book untouched.
    Status submit(const OrderRequest &request, std::vector<Trade> &trades);

    bool cancel_order(OrderId id) { return book_.cancel_order(id); }

    // Sets a resting order's price and total remaining quantity. Reducing the quantity at
    // the same price keeps queue priority; any other change cancels and re-submits the
    // order as GTC (losing priority, and possibly trading immediately).
    Status modify_order(OrderId id, Price new_price, Quantity new_quantity, std::vector<Trade> &trades);

    // conveniences that return the trades instead of filling a caller-owned buffer;
    // a rejected order simply returns no trades
    std::vector<Trade> submit_limit_order(OrderId id, Side side, Price price, Quantity quantity,
                                          TimeInForce tif = TimeInForce::GTC);
    std::vector<Trade> submit_market_order(OrderId id, Side side, Quantity quantity);

    const OrderBook &book() const { return book_; }

private:
    OrderBook book_;
};

#endif // MATCHING_ENGINE_HPP
