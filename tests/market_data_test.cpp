// Only the input validation is tested here: it needs no network and no python. The symbol is
// interpolated into a shell command, so anything that is not ticker-like must be refused
// before a process is started.
#include "market_data/market_data_interface.hpp"
#include "test_util.hpp"

int main()
{
    CHECK(!get_stock_price(""));
    CHECK(!get_stock_price("AAPL; touch /tmp/pwned"));
    CHECK(!get_stock_price("$(touch /tmp/pwned)"));
    CHECK(!get_stock_price("A B"));
    CHECK(!get_stock_price("`id`"));
    CHECK(!get_stock_price("A|B"));
    CHECK(!get_stock_price("WAYTOOLONGSYMBOL1"));
    return TEST_MAIN_RESULT();
}
