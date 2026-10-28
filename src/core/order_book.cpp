#include "order_book.hpp"

// ---------------------------------------------------------------- SideBook

std::int64_t OrderBook::SideBook::next_set(std::int64_t from) const
{
    std::size_t word = static_cast<std::size_t>(from) >> 6;
    if (word >= occupied.size())
        return -1;

    std::uint64_t bits = occupied[word] & (~std::uint64_t{0} << (from & 63));
    while (true)
    {
        if (bits)
            return static_cast<std::int64_t>((word << 6) + __builtin_ctzll(bits));
        if (++word >= occupied.size())
            return -1;
        bits = occupied[word];
    }
}

std::int64_t OrderBook::SideBook::prev_set(std::int64_t from) const
{
    if (from < 0)
        return -1;

    std::size_t word = static_cast<std::size_t>(from) >> 6;
    std::uint64_t bits = occupied[word] & (~std::uint64_t{0} >> (63 - (from & 63)));
    while (true)
    {
        if (bits)
            return static_cast<std::int64_t>((word << 6) + 63 - __builtin_clzll(bits));
        if (word == 0)
            return -1;
        bits = occupied[--word];
    }
}

// ----------------------------------------------------------------- IdTable

OrderBook::IdTable::IdTable() : slots_(1u << 12, Slot{kReservedOrderId, 0}), shift_(64 - 12) {}

std::size_t OrderBook::IdTable::home(OrderId key) const
{
    return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> shift_);
}

const std::uint32_t *OrderBook::IdTable::find(OrderId key) const
{
    const std::size_t mask = slots_.size() - 1;
    for (std::size_t i = home(key);; i = (i + 1) & mask)
    {
        if (slots_[i].key == key)
            return &slots_[i].value;
        if (slots_[i].key == kReservedOrderId)
            return nullptr;
    }
}

void OrderBook::IdTable::insert(OrderId key, std::uint32_t value)
{
    if ((size_ + 1) * 2 > slots_.size())
        grow();

    const std::size_t mask = slots_.size() - 1;
    std::size_t i = home(key);
    while (slots_[i].key != kReservedOrderId)
        i = (i + 1) & mask;
    slots_[i] = Slot{key, value};
    ++size_;
}

void OrderBook::IdTable::erase(OrderId key)
{
    const std::size_t mask = slots_.size() - 1;
    std::size_t i = home(key);
    while (slots_[i].key != key)
        i = (i + 1) & mask;

    // backward-shift deletion: pull later entries of the probe chain into the hole
    std::size_t j = i;
    while (true)
    {
        j = (j + 1) & mask;
        if (slots_[j].key == kReservedOrderId)
            break;

        const std::size_t h = home(slots_[j].key);
        const bool movable = i <= j ? (h <= i || h > j) : (h <= i && h > j);
        if (movable)
        {
            slots_[i] = slots_[j];
            i = j;
        }
    }
    slots_[i].key = kReservedOrderId;
    --size_;
}

void OrderBook::IdTable::grow()
{
    std::vector<Slot> old(slots_.size() * 2, Slot{kReservedOrderId, 0});
    old.swap(slots_);
    --shift_;
    size_ = 0;
    for (const Slot &slot : old)
    {
        if (slot.key != kReservedOrderId)
            insert(slot.key, slot.value);
    }
}

// --------------------------------------------------------------- OrderBook

OrderBook::OrderBook(Price max_price) : max_price_(max_price)
{
    const std::size_t levels = static_cast<std::size_t>(max_price) + 1;
    for (SideBook *sb : {&bids_, &asks_})
    {
        sb->levels.assign(levels, Level{});
        sb->occupied.assign((levels + 63) / 64, 0);
    }
    nodes_.reserve(1u << 16);
}

std::uint32_t OrderBook::allocate_node()
{
    if (free_head_ != kNil)
    {
        const std::uint32_t index = free_head_;
        free_head_ = nodes_[index].next;
        return index;
    }
    nodes_.emplace_back();
    return static_cast<std::uint32_t>(nodes_.size() - 1);
}

bool OrderBook::add_order(const Order &order)
{
    if (order.quantity <= 0 || order.price < 1 || order.price > max_price_ ||
        order.id == kReservedOrderId || ids_.find(order.id))
        return false;

    SideBook &sb = side_book(order.side);
    const std::uint32_t index = allocate_node();
    Level &level = sb.levels[static_cast<std::size_t>(order.price)];

    nodes_[index] = Node{order.id, order.quantity, level.tail, kNil,
                         static_cast<std::uint32_t>(order.price), order.side};
    if (level.tail != kNil)
    {
        nodes_[level.tail].next = index;
    }
    else
    {
        level.head = index;
        sb.occupied[static_cast<std::size_t>(order.price) >> 6] |= std::uint64_t{1} << (order.price & 63);
        if (sb.best < 0 || (order.side == Side::Buy ? order.price > sb.best : order.price < sb.best))
            sb.best = order.price;
    }
    level.tail = index;
    level.total += order.quantity;

    ids_.insert(order.id, index);
    ++order_count_;
    return true;
}

void OrderBook::remove_node(std::uint32_t index)
{
    Node &node = nodes_[index];
    SideBook &sb = side_book(node.side);
    Level &level = sb.levels[node.price];

    if (node.prev != kNil)
        nodes_[node.prev].next = node.next;
    else
        level.head = node.next;
    if (node.next != kNil)
        nodes_[node.next].prev = node.prev;
    else
        level.tail = node.prev;
    level.total -= node.quantity;

    if (level.head == kNil)
    {
        sb.occupied[node.price >> 6] &= ~(std::uint64_t{1} << (node.price & 63));
        if (static_cast<std::int64_t>(node.price) == sb.best)
            sb.best = node.side == Side::Buy ? sb.prev_set(sb.best - 1) : sb.next_set(sb.best + 1);
    }

    ids_.erase(node.id);
    node.next = free_head_;
    free_head_ = index;
    --order_count_;
}

bool OrderBook::cancel_order(OrderId id)
{
    const std::uint32_t *index = ids_.find(id);
    if (!index)
        return false;
    remove_node(*index);
    return true;
}

bool OrderBook::reduce_order(OrderId id, Quantity new_quantity)
{
    const std::uint32_t *index = ids_.find(id);
    if (!index)
        return false;

    Node &node = nodes_[*index];
    if (new_quantity <= 0 || new_quantity >= node.quantity)
        return false;

    side_book(node.side).levels[node.price].total -= node.quantity - new_quantity;
    node.quantity = new_quantity;
    return true;
}

std::optional<Order> OrderBook::find_order(OrderId id) const
{
    const std::uint32_t *index = ids_.find(id);
    if (!index)
        return std::nullopt;
    const Node &node = nodes_[*index];
    return Order{node.id, node.side, static_cast<Price>(node.price), node.quantity};
}

std::optional<Price> OrderBook::best_bid() const
{
    if (bids_.best < 0)
        return std::nullopt;
    return bids_.best;
}

std::optional<Price> OrderBook::best_ask() const
{
    if (asks_.best < 0)
        return std::nullopt;
    return asks_.best;
}

Quantity OrderBook::depth_at(Side side, Price price) const
{
    if (price < 1 || price > max_price_)
        return 0;
    return side_book(side).levels[static_cast<std::size_t>(price)].total;
}

Order OrderBook::peek_best(Side side) const
{
    const SideBook &sb = side_book(side);
    const Node &node = nodes_[sb.levels[static_cast<std::size_t>(sb.best)].head];
    return Order{node.id, side, static_cast<Price>(node.price), node.quantity};
}

void OrderBook::consume_best(Side side, Quantity quantity)
{
    SideBook &sb = side_book(side);
    Level &level = sb.levels[static_cast<std::size_t>(sb.best)];
    Node &node = nodes_[level.head];

    if (quantity >= node.quantity)
    {
        remove_node(level.head);
    }
    else
    {
        node.quantity -= quantity;
        level.total -= quantity;
    }
}

Quantity OrderBook::crossable_quantity(Side taker_side, Price limit, Quantity needed) const
{
    Quantity total = 0;
    if (taker_side == Side::Buy)
    {
        for (std::int64_t p = asks_.best; p >= 0 && p <= limit && total < needed; p = asks_.next_set(p + 1))
            total += asks_.levels[static_cast<std::size_t>(p)].total;
    }
    else
    {
        for (std::int64_t p = bids_.best; p >= 0 && p >= limit && total < needed; p = bids_.prev_set(p - 1))
            total += bids_.levels[static_cast<std::size_t>(p)].total;
    }
    return total;
}
