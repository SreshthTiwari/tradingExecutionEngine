#include "risk_checks.hpp"

bool RiskChecks::check_order_size(Quantity size, Quantity max_size)
{
    return size > 0 && size <= max_size;
}

bool RiskChecks::check_price_limits(Price price, Price min_price, Price max_price)
{
    return price >= min_price && price <= max_price;
}

bool RiskChecks::validate_order(Quantity size, Price price, const RiskLimits &limits)
{
    return check_order_size(size, limits.max_order_size) &&
           check_price_limits(price, limits.min_price, limits.max_price);
}
