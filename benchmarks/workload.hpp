#ifndef WORKLOAD_HPP
#define WORKLOAD_HPP

#include <random>
#include <vector>

#include "core/matching_engine.hpp"

// deterministic synthetic order flow: limit orders clustered around a mid price,
// so a realistic share of them cross the spread and the rest rest on the book
struct Message
{
    bool cancel;
    OrderId id; // for a cancel: id of an earlier order
    Side side;
    Price price;
    Quantity quantity;
};

inline std::vector<Message> make_workload(std::size_t count, unsigned seed = 42)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> offset(-50, 50);
    std::uniform_int_distribution<int> qty(1, 100);
    std::uniform_int_distribution<int> pct(0, 99);

    std::vector<Message> msgs;
    msgs.reserve(count);
    OrderId next_id = 1;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (next_id > 1 && pct(rng) < 10) // ~10% cancels of a random earlier order
        {
            std::uniform_int_distribution<OrderId> pick(1, next_id - 1);
            msgs.push_back({true, pick(rng), Side::Buy, 0, 0});
        }
        else
        {
            Side side = pct(rng) < 50 ? Side::Buy : Side::Sell;
            msgs.push_back({false, next_id++, side, 10000 + offset(rng), qty(rng)});
        }
    }
    return msgs;
}

// Applies one message. By default trades go into a caller-owned buffer that is reused across
// calls (the engine's native, allocation-free API). Define BENCH_VECTOR_API to use the
// convenience wrappers that return a fresh std::vector per call instead.
inline void apply(MatchingEngine &engine, const Message &m, std::size_t &trade_count, std::vector<Trade> &buffer)
{
    if (m.cancel)
    {
        engine.cancel_order(m.id);
        return;
    }
#ifdef BENCH_VECTOR_API
    (void)buffer;
    trade_count += engine.submit_limit_order(m.id, m.side, m.price, m.quantity).size();
#else
    buffer.clear();
    engine.submit(OrderRequest{m.id, m.side, OrderType::Limit, TimeInForce::GTC, m.price, m.quantity}, buffer);
    trade_count += buffer.size();
#endif
}

#endif // WORKLOAD_HPP
