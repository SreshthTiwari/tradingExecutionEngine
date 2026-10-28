#include "core/matching_engine.hpp"
#include "core/trade_execution.hpp"
#include "test_util.hpp"

static void no_cross_rests()
{
    MatchingEngine me;
    CHECK(me.submit_limit_order(1, Side::Buy, 99, 10).empty());
    CHECK(me.submit_limit_order(2, Side::Sell, 101, 10).empty());
    CHECK(me.book().best_bid() == 99);
    CHECK(me.book().best_ask() == 101);
    CHECK(me.book().order_count() == 2);
}

static void exact_fill()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 10);
    auto trades = me.submit_limit_order(2, Side::Buy, 100, 10);
    CHECK(trades.size() == 1);
    CHECK(trades[0].taker_id == 2 && trades[0].maker_id == 1);
    CHECK(trades[0].price == 100 && trades[0].quantity == 10);
    CHECK(me.book().order_count() == 0);
}

static void partial_fill_rests_remainder()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 4);
    auto trades = me.submit_limit_order(2, Side::Buy, 100, 10);
    CHECK(trades.size() == 1 && trades[0].quantity == 4);
    CHECK(me.book().best_bid() == 100);
    CHECK(me.book().depth_at(Side::Buy, 100) == 6);
    CHECK(!me.book().best_ask());
}

static void partial_fill_of_resting_order()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 10);
    auto trades = me.submit_limit_order(2, Side::Buy, 100, 3);
    CHECK(trades.size() == 1 && trades[0].quantity == 3);
    CHECK(me.book().depth_at(Side::Sell, 100) == 7);
    CHECK(me.book().order_count() == 1);
}

static void sweeps_multiple_levels_best_price_first()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 102, 5);
    me.submit_limit_order(2, Side::Sell, 100, 5);
    me.submit_limit_order(3, Side::Sell, 101, 5);
    auto trades = me.submit_limit_order(4, Side::Buy, 102, 12);
    CHECK(trades.size() == 3);
    CHECK(trades[0].price == 100 && trades[0].maker_id == 2);
    CHECK(trades[1].price == 101 && trades[1].maker_id == 3);
    CHECK(trades[2].price == 102 && trades[2].quantity == 2);
    CHECK(me.book().depth_at(Side::Sell, 102) == 3);
}

static void limit_price_is_respected()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 5);
    me.submit_limit_order(2, Side::Sell, 105, 5);
    auto trades = me.submit_limit_order(3, Side::Buy, 102, 10);
    CHECK(trades.size() == 1 && trades[0].price == 100);
    CHECK(me.book().best_bid() == 102);
    CHECK(me.book().best_ask() == 105);
}

static void fifo_within_level()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Buy, 100, 5);
    me.submit_limit_order(2, Side::Buy, 100, 5);
    auto trades = me.submit_limit_order(3, Side::Sell, 100, 7);
    CHECK(trades.size() == 2);
    CHECK(trades[0].maker_id == 1 && trades[0].quantity == 5);
    CHECK(trades[1].maker_id == 2 && trades[1].quantity == 2);
}

static void sell_side_matches_best_bid()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Buy, 99, 5);
    me.submit_limit_order(2, Side::Buy, 101, 5);
    auto trades = me.submit_limit_order(3, Side::Sell, 99, 8);
    CHECK(trades.size() == 2);
    CHECK(trades[0].price == 101 && trades[0].quantity == 5);
    CHECK(trades[1].price == 99 && trades[1].quantity == 3);
}

static void cancel_and_invalid_orders()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Buy, 100, 5);
    CHECK(me.submit_limit_order(1, Side::Buy, 100, 5).empty()); // duplicate id ignored
    CHECK(me.book().depth_at(Side::Buy, 100) == 5);
    CHECK(me.submit_limit_order(2, Side::Buy, 100, 0).empty());
    CHECK(me.submit_limit_order(3, Side::Buy, -1, 5).empty());
    CHECK(me.book().order_count() == 1);

    CHECK(me.cancel_order(1));
    CHECK(me.submit_limit_order(4, Side::Sell, 100, 5).empty()); // nothing left to match
    CHECK(me.book().best_ask() == 100);
}

static void ioc_drops_remainder()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 4);
    auto trades = me.submit_limit_order(2, Side::Buy, 100, 10, TimeInForce::IOC);
    CHECK(trades.size() == 1 && trades[0].quantity == 4);
    CHECK(me.book().order_count() == 0); // the remaining 6 did not rest
    CHECK(me.submit_limit_order(3, Side::Buy, 90, 5, TimeInForce::IOC).empty());
    CHECK(me.book().order_count() == 0);
}

static void fok_all_or_nothing()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 5);
    me.submit_limit_order(2, Side::Sell, 101, 5);

    std::vector<Trade> trades;
    // only 5 available at <= 100: kill, leaving the book untouched
    CHECK(me.submit({3, Side::Buy, OrderType::Limit, TimeInForce::FOK, 100, 8}, trades) == Status::NotFillable);
    CHECK(trades.empty() && me.book().order_count() == 2);
    CHECK(me.book().depth_at(Side::Sell, 100) == 5);

    // 10 available at <= 101: fills across both levels
    CHECK(me.submit({4, Side::Buy, OrderType::Limit, TimeInForce::FOK, 101, 8}, trades) == Status::Ok);
    CHECK(trades.size() == 2 && trades[0].quantity == 5 && trades[1].quantity == 3);
    CHECK(me.book().depth_at(Side::Sell, 101) == 2);
}

static void market_orders()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 5);
    me.submit_limit_order(2, Side::Sell, 200, 5);
    me.submit_limit_order(3, Side::Buy, 50, 5);

    // a market buy takes whatever is there, at any price, and never rests
    auto trades = me.submit_market_order(4, Side::Buy, 8);
    CHECK(trades.size() == 2);
    CHECK(trades[0].price == 100 && trades[0].quantity == 5);
    CHECK(trades[1].price == 200 && trades[1].quantity == 3);
    CHECK(!me.book().best_bid() || *me.book().best_bid() == 50);

    // a market sell larger than the book: fills what it can, the rest is dropped
    trades = me.submit_market_order(5, Side::Sell, 100);
    CHECK(trades.size() == 1 && trades[0].price == 50 && trades[0].quantity == 5);
    CHECK(me.book().order_count() == 1); // only the 2 remaining @ 200

    // empty book: no trades, nothing rests
    MatchingEngine empty;
    CHECK(empty.submit_market_order(1, Side::Buy, 10).empty());
    CHECK(empty.book().order_count() == 0);

    // market fill-or-kill respects total available liquidity
    MatchingEngine thin;
    thin.submit_limit_order(1, Side::Sell, 100, 5);
    std::vector<Trade> out;
    CHECK(thin.submit({2, Side::Buy, OrderType::Market, TimeInForce::FOK, 0, 6}, out) == Status::NotFillable);
    CHECK(thin.submit({3, Side::Buy, OrderType::Market, TimeInForce::FOK, 0, 5}, out) == Status::Ok);
    CHECK(out.size() == 1);
}

static void modify_orders()
{
    MatchingEngine me;
    std::vector<Trade> trades;
    me.submit_limit_order(1, Side::Buy, 100, 10);
    me.submit_limit_order(2, Side::Buy, 100, 10);

    // reducing quantity at the same price keeps queue priority
    CHECK(me.modify_order(1, 100, 4, trades) == Status::Ok);
    CHECK(me.book().depth_at(Side::Buy, 100) == 14);
    auto fills = me.submit_limit_order(3, Side::Sell, 100, 6);
    CHECK(fills.size() == 2 && fills[0].maker_id == 1 && fills[0].quantity == 4 && fills[1].maker_id == 2);

    // increasing quantity loses priority
    MatchingEngine me2;
    me2.submit_limit_order(1, Side::Buy, 100, 5);
    me2.submit_limit_order(2, Side::Buy, 100, 5);
    CHECK(me2.modify_order(1, 100, 8, trades) == Status::Ok);
    fills = me2.submit_limit_order(3, Side::Sell, 100, 5);
    CHECK(fills.size() == 1 && fills[0].maker_id == 2);

    // changing price moves the order; a price that crosses trades immediately
    MatchingEngine me3;
    me3.submit_limit_order(1, Side::Sell, 105, 5);
    me3.submit_limit_order(2, Side::Buy, 100, 5);
    trades.clear();
    CHECK(me3.modify_order(2, 105, 5, trades) == Status::Ok);
    CHECK(trades.size() == 1 && trades[0].taker_id == 2 && trades[0].maker_id == 1 && trades[0].price == 105);
    CHECK(me3.book().order_count() == 0);

    // errors
    CHECK(me3.modify_order(99, 100, 5, trades) == Status::NotFound);
    me3.submit_limit_order(7, Side::Buy, 100, 5);
    CHECK(me3.modify_order(7, 100, 0, trades) == Status::InvalidOrder);
    CHECK(me3.modify_order(7, 0, 5, trades) == Status::InvalidOrder);
    CHECK(me3.book().depth_at(Side::Buy, 100) == 5); // unchanged by the failed modifies
}

static void statuses_and_price_band()
{
    MatchingEngine me(1000);
    std::vector<Trade> trades;
    CHECK(me.submit({1, Side::Buy, OrderType::Limit, TimeInForce::GTC, 1000, 5}, trades) == Status::Ok);
    CHECK(me.submit({2, Side::Buy, OrderType::Limit, TimeInForce::GTC, 1001, 5}, trades) == Status::InvalidOrder);
    CHECK(me.submit({3, Side::Buy, OrderType::Limit, TimeInForce::GTC, 0, 5}, trades) == Status::InvalidOrder);
    CHECK(me.submit({1, Side::Buy, OrderType::Limit, TimeInForce::GTC, 10, 5}, trades) == Status::DuplicateId);
    CHECK(me.submit({kReservedOrderId, Side::Buy, OrderType::Limit, TimeInForce::GTC, 10, 5}, trades) ==
          Status::InvalidOrder);
    CHECK(me.book().order_count() == 1);

    // a filled order's id can be reused
    me.submit_limit_order(10, Side::Sell, 1000, 5);
    CHECK(me.book().order_count() == 0);
    CHECK(me.submit({10, Side::Sell, OrderType::Limit, TimeInForce::GTC, 900, 1}, trades) == Status::Ok);
}

static void reuses_caller_buffer()
{
    MatchingEngine me;
    me.submit_limit_order(1, Side::Sell, 100, 5);
    me.submit_limit_order(2, Side::Sell, 101, 5);
    std::vector<Trade> trades;
    me.submit({3, Side::Buy, OrderType::Limit, TimeInForce::GTC, 100, 5}, trades);
    me.submit({4, Side::Buy, OrderType::Limit, TimeInForce::GTC, 101, 5}, trades);
    CHECK(trades.size() == 2); // submit appends rather than clearing
}

static void risk_gate()
{
    TradeExecution exec(RiskLimits{100, 1000, 5000});
    CHECK(exec.execute(1, Side::Sell, 2000, 50).has_value());
    CHECK(!exec.execute(2, Side::Buy, 2000, 101)); // too large
    CHECK(!exec.execute(3, Side::Buy, 999, 10));   // below min price
    CHECK(!exec.execute(4, Side::Buy, 5001, 10));  // above max price
    CHECK(!exec.execute(5, Side::Buy, 2000, 0));   // zero size
    auto result = exec.execute(6, Side::Buy, 2000, 20);
    CHECK(result && result->size() == 1 && (*result)[0].quantity == 20);

    // market orders are size-checked only; modifies are re-checked
    std::vector<Trade> trades;
    CHECK(exec.submit({7, Side::Buy, OrderType::Market, TimeInForce::IOC, 0, 10}, trades) == Status::Ok);
    CHECK(exec.submit({8, Side::Buy, OrderType::Market, TimeInForce::IOC, 0, 500}, trades) == Status::RiskRejected);
    exec.execute(9, Side::Buy, 1500, 10); // below the resting ask, so it rests
    CHECK(exec.modify(9, 6000, 10, trades) == Status::RiskRejected);
    CHECK(exec.modify(9, 1600, 10, trades) == Status::Ok);
    CHECK(exec.cancel(9));
}

int main()
{
    no_cross_rests();
    exact_fill();
    partial_fill_rests_remainder();
    partial_fill_of_resting_order();
    sweeps_multiple_levels_best_price_first();
    limit_price_is_respected();
    fifo_within_level();
    sell_side_matches_best_bid();
    cancel_and_invalid_orders();
    ioc_drops_remainder();
    fok_all_or_nothing();
    market_orders();
    modify_orders();
    statuses_and_price_band();
    reuses_caller_buffer();
    risk_gate();
    return TEST_MAIN_RESULT();
}
