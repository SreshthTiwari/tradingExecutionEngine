#ifndef REFERENCE_BOOK_HPP
#define REFERENCE_BOOK_HPP

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "core/types.hpp"

// Deliberately naive matching engine used as a test oracle: std::map of std::deque per price
// level and no cleverness. It implements the same rules as MatchingEngine, so the two must
// agree on every message.
class ReferenceBook
{
public:
    explicit ReferenceBook(Price max_price) : max_price_(max_price) {}

    Status submit(const OrderRequest &req, std::vector<Trade> &trades)
    {
        const bool market = req.type == OrderType::Market;
        if (req.quantity <= 0 || req.id == kReservedOrderId)
            return Status::InvalidOrder;
        if (!market && (req.price < 1 || req.price > max_price_))
            return Status::InvalidOrder;
        if (where_.count(req.id))
            return Status::DuplicateId;

        const Price limit = !market ? req.price : (req.side == Side::Buy ? max_price_ : 1);
        const TimeInForce tif = market && req.tif == TimeInForce::GTC ? TimeInForce::IOC : req.tif;

        if (tif == TimeInForce::FOK && fillable(req.side, limit) < req.quantity)
            return Status::NotFillable;

        Quantity remaining = req.quantity;
        if (req.side == Side::Buy)
            remaining = sweep(asks_, req, limit, remaining, trades);
        else
            remaining = sweep(bids_, req, limit, remaining, trades);

        if (remaining > 0 && !market && tif == TimeInForce::GTC)
        {
            if (req.side == Side::Buy)
                bids_[req.price].push_back(Order{req.id, req.side, req.price, remaining});
            else
                asks_[req.price].push_back(Order{req.id, req.side, req.price, remaining});
            where_[req.id] = {req.side, req.price};
        }
        return Status::Ok;
    }

    bool cancel(OrderId id)
    {
        auto found = where_.find(id);
        if (found == where_.end())
            return false;
        if (found->second.first == Side::Buy)
            erase_from(bids_, found->second.second, id);
        else
            erase_from(asks_, found->second.second, id);
        where_.erase(found);
        return true;
    }

    Status modify(OrderId id, Price price, Quantity quantity, std::vector<Trade> &trades)
    {
        auto found = where_.find(id);
        if (found == where_.end())
            return Status::NotFound;
        if (quantity <= 0 || price < 1 || price > max_price_)
            return Status::InvalidOrder;

        const Side side = found->second.first;
        const Price old_price = found->second.second;
        Order *order = find_order(side, old_price, id);

        if (price == old_price && quantity <= order->quantity)
        {
            order->quantity = quantity; // same or smaller: keeps queue position
            return Status::Ok;
        }
        cancel(id);
        return submit(OrderRequest{id, side, OrderType::Limit, TimeInForce::GTC, price, quantity}, trades);
    }

    std::optional<Price> best_bid() const
    {
        if (bids_.empty())
            return std::nullopt;
        return bids_.begin()->first;
    }
    std::optional<Price> best_ask() const
    {
        if (asks_.empty())
            return std::nullopt;
        return asks_.begin()->first;
    }
    Quantity depth(Side side, Price price) const
    {
        Quantity total = 0;
        if (side == Side::Buy)
        {
            auto it = bids_.find(price);
            if (it != bids_.end())
                for (const Order &o : it->second)
                    total += o.quantity;
        }
        else
        {
            auto it = asks_.find(price);
            if (it != asks_.end())
                for (const Order &o : it->second)
                    total += o.quantity;
        }
        return total;
    }
    std::size_t order_count() const { return where_.size(); }

private:
    using Bids = std::map<Price, std::deque<Order>, std::greater<Price>>;
    using Asks = std::map<Price, std::deque<Order>>;

    Quantity fillable(Side taker, Price limit) const
    {
        Quantity total = 0;
        if (taker == Side::Buy)
        {
            for (const auto &level : asks_)
                if (level.first <= limit)
                    for (const Order &o : level.second)
                        total += o.quantity;
        }
        else
        {
            for (const auto &level : bids_)
                if (level.first >= limit)
                    for (const Order &o : level.second)
                        total += o.quantity;
        }
        return total;
    }

    template <typename Book>
    Quantity sweep(Book &book, const OrderRequest &req, Price limit, Quantity remaining, std::vector<Trade> &trades)
    {
        while (remaining > 0 && !book.empty())
        {
            auto level = book.begin();
            const bool crosses = req.side == Side::Buy ? level->first <= limit : level->first >= limit;
            if (!crosses)
                break;

            std::deque<Order> &queue = level->second;
            while (remaining > 0 && !queue.empty())
            {
                Order &maker = queue.front();
                const Quantity fill = std::min(remaining, maker.quantity);
                trades.push_back(Trade{req.id, maker.id, level->first, fill});
                remaining -= fill;
                maker.quantity -= fill;
                if (maker.quantity == 0)
                {
                    where_.erase(maker.id);
                    queue.pop_front();
                }
            }
            if (queue.empty())
                book.erase(level);
        }
        return remaining;
    }

    template <typename Book>
    void erase_from(Book &book, Price price, OrderId id)
    {
        auto level = book.find(price);
        auto &queue = level->second;
        queue.erase(std::find_if(queue.begin(), queue.end(), [&](const Order &o) { return o.id == id; }));
        if (queue.empty())
            book.erase(level);
    }

    Order *find_order(Side side, Price price, OrderId id)
    {
        auto &queue = side == Side::Buy ? bids_.at(price) : asks_.at(price);
        return &*std::find_if(queue.begin(), queue.end(), [&](const Order &o) { return o.id == id; });
    }

    Price max_price_;
    Bids bids_;
    Asks asks_;
    std::map<OrderId, std::pair<Side, Price>> where_;
};

#endif // REFERENCE_BOOK_HPP
