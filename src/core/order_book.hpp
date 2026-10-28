#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "types.hpp"

// Price-time priority book built for speed: no allocation on the hot path once warm.
//  - price levels are a flat array indexed by price tick (valid prices are 1..max_price)
//  - a bitmap of non-empty levels finds the next best price after a level empties
//  - resting orders live in a pooled node array, chained per level in FIFO order
//  - an open-addressing table maps order id -> node for O(1) cancel
class OrderBook
{
public:
    static constexpr Price kDefaultMaxPrice = 1 << 18;

    explicit OrderBook(Price max_price = kDefaultMaxPrice);

    Price max_price() const { return max_price_; }

    // rests an order; returns false (book unchanged) if quantity <= 0, the price is outside
    // 1..max_price, or the id is reserved or already resting
    bool add_order(const Order &order);

    // removes a resting order by id, returns false if it is not on the book
    bool cancel_order(OrderId id);

    // shrinks a resting order in place, keeping its queue position.
    // requires 0 < new_quantity < current quantity
    bool reduce_order(OrderId id, Quantity new_quantity);

    std::optional<Order> find_order(OrderId id) const;
    bool contains(OrderId id) const { return ids_.find(id) != nullptr; }

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    // total resting quantity at a price level (0 if none)
    Quantity depth_at(Side side, Price price) const;

    std::size_t order_count() const { return order_count_; }

    // --- primitives for the matching engine ---

    // front order of the best level on `side`; the side must not be empty
    Order peek_best(Side side) const;

    // takes `quantity` from the front order of the best level on `side`,
    // removing it if fully consumed; quantity must be <= that order's quantity
    void consume_best(Side side, Quantity quantity);

    // how much a taker on `taker_side` could fill at prices no worse than `limit`,
    // stopping early once `needed` is reached
    Quantity crossable_quantity(Side taker_side, Price limit, Quantity needed) const;

private:
    static constexpr std::uint32_t kNil = 0xFFFFFFFFu;

    struct Node
    {
        OrderId id;
        Quantity quantity;
        std::uint32_t prev;
        std::uint32_t next;
        std::uint32_t price;
        Side side;
    };

    struct Level
    {
        std::uint32_t head = kNil;
        std::uint32_t tail = kNil;
        Quantity total = 0;
    };

    struct SideBook
    {
        std::vector<Level> levels;
        std::vector<std::uint64_t> occupied; // bit p set <=> levels[p] is non-empty
        std::int64_t best = -1;              // best occupied price, -1 if empty

        std::int64_t next_set(std::int64_t from) const; // smallest occupied price >= from, or -1
        std::int64_t prev_set(std::int64_t from) const; // largest occupied price <= from, or -1
    };

    // open-addressing hash table, OrderId -> node index (linear probing, backward-shift delete)
    class IdTable
    {
    public:
        IdTable();
        const std::uint32_t *find(OrderId key) const;
        void insert(OrderId key, std::uint32_t value); // key must be absent
        void erase(OrderId key);                       // key must be present

    private:
        struct Slot
        {
            OrderId key;
            std::uint32_t value;
        };
        std::size_t home(OrderId key) const;
        void grow();

        std::vector<Slot> slots_;
        std::size_t size_ = 0;
        unsigned shift_ = 0;
    };

    SideBook &side_book(Side side) { return side == Side::Buy ? bids_ : asks_; }
    const SideBook &side_book(Side side) const { return side == Side::Buy ? bids_ : asks_; }

    std::uint32_t allocate_node();
    void remove_node(std::uint32_t index);

    Price max_price_;
    SideBook bids_;
    SideBook asks_;
    std::vector<Node> nodes_;
    std::uint32_t free_head_ = kNil;
    IdTable ids_;
    std::size_t order_count_ = 0;
};

#endif // ORDER_BOOK_HPP
