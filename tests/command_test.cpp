#include "core/command.hpp"
#include "test_util.hpp"

static void parses_valid_commands()
{
    auto c = parse_command("NEW 7 B 10050 25");
    CHECK(c && c->kind == Command::Kind::New && c->order.id == 7 && c->order.side == Side::Buy);
    CHECK(c && c->order.type == OrderType::Limit && c->order.tif == TimeInForce::GTC);
    CHECK(c && c->order.price == 10050 && c->order.quantity == 25);

    c = parse_command("  new 8 s 99 3 fok \r");
    CHECK(c && c->order.side == Side::Sell && c->order.tif == TimeInForce::FOK);

    c = parse_command("MKT 9 S 40");
    CHECK(c && c->order.type == OrderType::Market && c->order.tif == TimeInForce::IOC && c->order.quantity == 40);
    c = parse_command("MKT 9 B 40 FOK");
    CHECK(c && c->order.tif == TimeInForce::FOK);

    c = parse_command("CANCEL 12");
    CHECK(c && c->kind == Command::Kind::Cancel && c->order.id == 12);

    c = parse_command("MODIFY 12 10060 5");
    CHECK(c && c->kind == Command::Kind::Modify && c->order.id == 12 && c->order.price == 10060 &&
          c->order.quantity == 5);
}

static void rejects_malformed_lines()
{
    CHECK(!parse_command(""));
    CHECK(!parse_command("   "));
    CHECK(!parse_command("# a comment"));
    CHECK(!parse_command("HELLO 1 2 3"));
    CHECK(!parse_command("NEW 1 B 100"));          // missing quantity
    CHECK(!parse_command("NEW 1 B 100 5 GTC EXTRA"));
    CHECK(!parse_command("NEW 1 X 100 5"));        // bad side
    CHECK(!parse_command("NEW 1 B 100 5 DAY"));    // bad time in force
    CHECK(!parse_command("NEW one B 100 5"));
    CHECK(!parse_command("NEW -1 B 100 5"));       // ids are unsigned
    CHECK(!parse_command("NEW 1 B 10.5 5"));       // prices are integer ticks
    CHECK(!parse_command("NEW 1 B 100 5x"));
    CHECK(!parse_command("NEW 1 B 99999999999999999999 5")); // overflow
    CHECK(!parse_command("MKT 1 B 10 GTC"));       // market orders cannot rest
    CHECK(!parse_command("CANCEL"));
    CHECK(!parse_command("MODIFY 1 100"));
}

static void applies_commands()
{
    MatchingEngine me;
    std::vector<Trade> trades;
    CHECK(apply_command(me, *parse_command("NEW 1 S 100 10"), trades) == Status::Ok);
    CHECK(apply_command(me, *parse_command("MKT 2 B 4"), trades) == Status::Ok);
    CHECK(trades.size() == 1 && trades[0].quantity == 4);
    CHECK(apply_command(me, *parse_command("MODIFY 1 100 3"), trades) == Status::Ok);
    CHECK(me.book().depth_at(Side::Sell, 100) == 3);
    CHECK(apply_command(me, *parse_command("CANCEL 1"), trades) == Status::Ok);
    CHECK(apply_command(me, *parse_command("CANCEL 1"), trades) == Status::NotFound);
}

int main()
{
    parses_valid_commands();
    rejects_malformed_lines();
    applies_commands();
    return TEST_MAIN_RESULT();
}
