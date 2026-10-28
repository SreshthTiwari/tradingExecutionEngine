// Differential test: drive MatchingEngine and a naive ReferenceBook with the same random
// message stream and require identical statuses, trades and book state at every step.
#include <random>

#include "core/matching_engine.hpp"
#include "reference_book.hpp"
#include "test_util.hpp"

namespace
{
bool same_trades(const std::vector<Trade> &a, const std::vector<Trade> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].taker_id != b[i].taker_id || a[i].maker_id != b[i].maker_id || a[i].price != b[i].price ||
            a[i].quantity != b[i].quantity)
            return false;
    }
    return true;
}

// returns false (after reporting) on the first divergence
bool run(unsigned seed, std::size_t steps, Price mid, int spread, Price max_price)
{
    std::mt19937_64 rng(seed);
    auto uniform = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    MatchingEngine engine(max_price);
    ReferenceBook reference(max_price);
    OrderId next_id = 1;
    std::vector<Trade> got, want;

    auto fail = [&](std::size_t step, const char *what) {
        std::cerr << "seed " << seed << " step " << step << ": " << what << "\n";
        ++failures();
        return false;
    };

    for (std::size_t step = 0; step < steps; ++step)
    {
        got.clear();
        want.clear();
        Status a, b;

        const int roll = uniform(0, 99);
        if (roll < 12) // cancel (often an id that is gone, or never existed)
        {
            const OrderId id = static_cast<OrderId>(uniform(1, static_cast<int>(next_id) + 2));
            if (engine.cancel_order(id) != reference.cancel(id))
                return fail(step, "cancel result differs");
        }
        else if (roll < 25) // modify
        {
            const OrderId id = static_cast<OrderId>(uniform(1, static_cast<int>(next_id)));
            Price price = mid + uniform(-spread, spread);
            Quantity qty = uniform(1, 60);
            if (const auto current = engine.book().find_order(id); current && uniform(0, 99) < 60)
            {
                // exercise the in-place path: same price with equal or smaller quantity
                price = current->price;
                qty = uniform(0, 1) ? current->quantity : uniform(1, static_cast<int>(current->quantity));
            }
            a = engine.modify_order(id, price, qty, got);
            b = reference.modify(id, price, qty, want);
            if (a != b)
                return fail(step, "modify status differs");
        }
        else
        {
            OrderRequest req{next_id, uniform(0, 1) ? Side::Buy : Side::Sell, OrderType::Limit, TimeInForce::GTC,
                             mid + uniform(-spread, spread), uniform(1, 60)};

            const int kind = uniform(0, 99);
            if (kind < 12)
                req.tif = TimeInForce::IOC;
            else if (kind < 20)
                req.tif = TimeInForce::FOK;
            else if (kind < 30)
            {
                req.type = OrderType::Market;
                req.tif = uniform(0, 2) == 0 ? TimeInForce::FOK : (uniform(0, 1) ? TimeInForce::IOC : TimeInForce::GTC);
            }
            else if (kind < 32)
                req.price = uniform(0, 1) ? 0 : max_price + 1; // out of range
            else if (kind < 34)
                req.quantity = uniform(-3, 0);
            else if (kind < 37 && next_id > 1)
                req.id = static_cast<OrderId>(uniform(1, static_cast<int>(next_id) - 1)); // likely duplicate

            a = engine.submit(req, got);
            b = reference.submit(req, want);
            if (a != b)
                return fail(step, "submit status differs");
            if (req.id == next_id)
                ++next_id;
        }

        if (!same_trades(got, want))
            return fail(step, "trades differ");
        if (engine.book().best_bid() != reference.best_bid() || engine.book().best_ask() != reference.best_ask())
            return fail(step, "best bid/ask differs");
        if (engine.book().order_count() != reference.order_count())
            return fail(step, "order count differs");

        if (step % 250 == 0) // full depth comparison
        {
            for (Price p = mid - spread - 2; p <= mid + spread + 2; ++p)
            {
                if (p < 1)
                    continue;
                if (engine.book().depth_at(Side::Buy, p) != reference.depth(Side::Buy, p) ||
                    engine.book().depth_at(Side::Sell, p) != reference.depth(Side::Sell, p))
                    return fail(step, "depth differs");
            }
        }
    }
    return true;
}
} // namespace

int main()
{
    bool ok = true;
    // tight price band => constant crossing; wide band => deep resting book
    for (unsigned seed = 1; seed <= 8; ++seed)
        ok &= run(seed, 60000, 1000, 4, 4096);
    for (unsigned seed = 100; seed < 104; ++seed)
        ok &= run(seed, 60000, 1000, 80, 4096);
    // prices near both edges of the allowed range
    ok &= run(7, 40000, 5, 4, 64);
    ok &= run(8, 40000, 60, 4, 64);
    CHECK(ok);
    return TEST_MAIN_RESULT();
}
