// Replays the bundled sample file through the engine and through the reference book and
// requires identical trade streams, plus a few hand-checked replay results.
#include <fstream>
#include <sstream>

#include "core/command.hpp"
#include "market_data/replay.hpp"
#include "reference_book.hpp"
#include "test_util.hpp"

static void small_replay()
{
    std::istringstream in("# comment\n"
                          "\n"
                          "NEW 1 S 100 10\n"
                          "NEW 2 S 101 10\n"
                          "garbage line\n"
                          "NEW 3 B 101 15\n"
                          "CANCEL 99\n");
    MatchingEngine me;
    std::vector<Trade> seen;
    const ReplayStats stats = replay_stream(in, me, [&](const Trade &t) { seen.push_back(t); });

    CHECK(stats.lines == 5 && stats.malformed == 1 && stats.commands == 4);
    CHECK(stats.rejected == 1); // cancel of an unknown id
    CHECK(stats.trades == 2 && seen.size() == 2);
    CHECK(stats.volume == 15 && stats.last_trade_price == 101);
    CHECK(stats.notional == 100 * 10 + 101 * 5);
    CHECK(me.book().depth_at(Side::Sell, 101) == 5);
}

static void sample_file_matches_reference()
{
    std::ifstream file(SAMPLE_DATA_PATH);
    CHECK(file.good());

    std::vector<Command> commands;
    std::string line;
    while (std::getline(file, line))
        if (auto c = parse_command(line))
            commands.push_back(*c);
    CHECK(commands.size() > 1000);

    MatchingEngine engine;
    ReferenceBook reference(OrderBook::kDefaultMaxPrice);
    std::vector<Trade> got, want;
    std::size_t total_trades = 0;

    for (const Command &c : commands)
    {
        got.clear();
        want.clear();
        Status a = apply_command(engine, c, got);
        Status b = Status::Ok;
        switch (c.kind)
        {
        case Command::Kind::New:
            b = reference.submit(c.order, want);
            break;
        case Command::Kind::Cancel:
            b = reference.cancel(c.order.id) ? Status::Ok : Status::NotFound;
            break;
        case Command::Kind::Modify:
            b = reference.modify(c.order.id, c.order.price, c.order.quantity, want);
            break;
        }
        CHECK(a == b);
        CHECK(got.size() == want.size());
        for (std::size_t i = 0; i < got.size() && i < want.size(); ++i)
            CHECK(got[i].maker_id == want[i].maker_id && got[i].price == want[i].price &&
                  got[i].quantity == want[i].quantity);
        total_trades += got.size();
    }

    CHECK(total_trades > 100);
    CHECK(engine.book().best_bid() == reference.best_bid());
    CHECK(engine.book().best_ask() == reference.best_ask());
    CHECK(engine.book().order_count() == reference.order_count());
}

int main()
{
    small_replay();
    sample_file_matches_reference();
    return TEST_MAIN_RESULT();
}
