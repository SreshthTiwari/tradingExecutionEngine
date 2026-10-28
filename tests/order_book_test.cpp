#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

#include "core/order_book.hpp"
#include "test_util.hpp"

// stress the id -> order lookup (open addressing with backward-shift deletion) with a large,
// churning set of random ids, checking every live and dead id after each round
static void id_table_stress()
{
    std::mt19937_64 rng(7);
    OrderBook book(1000);
    std::unordered_map<OrderId, Price> live;

    for (int round = 0; round < 6; ++round)
    {
        for (int i = 0; i < 40000; ++i)
        {
            const OrderId id = rng() >> (rng() % 40); // mix of small and huge ids
            if (id == kReservedOrderId)
                continue;
            const Price price = 1 + static_cast<Price>(rng() % 1000);
            const bool added = book.add_order({id, i % 2 ? Side::Buy : Side::Sell, price, 1 + static_cast<Quantity>(rng() % 9)});
            CHECK(added == (live.count(id) == 0));
            if (added)
                live[id] = price;
        }

        std::vector<OrderId> ids;
        for (const auto &entry : live)
            ids.push_back(entry.first);
        std::shuffle(ids.begin(), ids.end(), rng);
        for (std::size_t i = 0; i < ids.size() * 2 / 3; ++i)
        {
            CHECK(book.cancel_order(ids[i]));
            live.erase(ids[i]);
        }

        CHECK(book.order_count() == live.size());
        for (const auto &entry : live)
        {
            const auto found = book.find_order(entry.first);
            CHECK(found && found->price == entry.second);
        }
        for (std::size_t i = 0; i < ids.size() * 2 / 3; ++i)
            CHECK(!book.contains(ids[i]));
    }
}

int main()
{
    OrderBook book;
    CHECK(!book.best_bid() && !book.best_ask());

    book.add_order({1, Side::Buy, 100, 10});
    book.add_order({2, Side::Buy, 101, 5});
    book.add_order({3, Side::Buy, 101, 7});
    book.add_order({4, Side::Sell, 105, 4});
    book.add_order({5, Side::Sell, 103, 6});

    CHECK(book.best_bid() == 101);
    CHECK(book.best_ask() == 103);
    CHECK(book.depth_at(Side::Buy, 101) == 12);
    CHECK(book.depth_at(Side::Sell, 999) == 0);
    CHECK(book.order_count() == 5);

    // cancelling one of two orders at a level keeps the level
    CHECK(book.cancel_order(2));
    CHECK(book.best_bid() == 101);
    CHECK(book.depth_at(Side::Buy, 101) == 7);

    // cancelling the last order at a level removes it
    CHECK(book.cancel_order(3));
    CHECK(book.best_bid() == 100);

    CHECK(book.cancel_order(5));
    CHECK(book.best_ask() == 105);

    // unknown / already cancelled ids
    CHECK(!book.cancel_order(5));
    CHECK(!book.cancel_order(12345));
    CHECK(book.order_count() == 2);

    id_table_stress();

    return TEST_MAIN_RESULT();
}
