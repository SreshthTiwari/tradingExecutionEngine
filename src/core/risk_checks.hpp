#ifndef RISK_CHECKS_HPP
#define RISK_CHECKS_HPP

#include "types.hpp"

struct RiskLimits
{
    Quantity max_order_size;
    Price min_price;
    Price max_price;
};

// pre-trade risk checks
class RiskChecks
{
public:
    static bool check_order_size(Quantity size, Quantity max_size);
    static bool check_price_limits(Price price, Price min_price, Price max_price);
    static bool validate_order(Quantity size, Price price, const RiskLimits &limits);
};

#endif // RISK_CHECKS_HPP
