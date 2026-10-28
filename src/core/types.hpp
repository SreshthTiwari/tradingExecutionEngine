#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstdint>

// prices are integer ticks (e.g. cents) to avoid floating point keys and comparisons
using Price = std::int64_t;
using Quantity = std::int64_t;
using OrderId = std::uint64_t;

// reserved as the empty marker inside the order book's id table; never a valid order id
constexpr OrderId kReservedOrderId = ~OrderId{0};

enum class Side
{
    Buy,
    Sell
};

inline Side opposite(Side side)
{
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

// a resting order
struct Order
{
    OrderId id;
    Side side;
    Price price;
    Quantity quantity; // remaining quantity
};

// one fill between an incoming (taker) order and a resting (maker) order
struct Trade
{
    OrderId taker_id;
    OrderId maker_id;
    Price price; // always the resting order's price
    Quantity quantity;
};

enum class OrderType
{
    Limit,
    Market // never rests; sweeps the book at any price
};

enum class TimeInForce
{
    GTC, // good till cancelled: unfilled remainder rests on the book
    IOC, // immediate or cancel: unfilled remainder is dropped
    FOK  // fill or kill: all of it fills immediately or nothing happens
};

struct OrderRequest
{
    OrderId id;
    Side side;
    OrderType type;
    TimeInForce tif;
    Price price; // ignored for market orders
    Quantity quantity;
};

enum class Status
{
    Ok,
    InvalidOrder, // bad quantity, price outside the book's range, or reserved id
    DuplicateId,  // id already rests on the book
    NotFound,     // cancel/modify of an id that is not resting
    NotFillable,  // fill-or-kill order that could not be filled in full
    RiskRejected  // refused by pre-trade risk checks
};

inline const char *to_string(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return "OK";
    case Status::InvalidOrder:
        return "INVALID_ORDER";
    case Status::DuplicateId:
        return "DUPLICATE_ID";
    case Status::NotFound:
        return "NOT_FOUND";
    case Status::NotFillable:
        return "NOT_FILLABLE";
    case Status::RiskRejected:
        return "RISK_REJECTED";
    }
    return "UNKNOWN";
}

#endif // TYPES_HPP
