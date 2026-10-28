#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "core/trade_execution.hpp"
#include "network/tcp_gateway.hpp"

static TcpGateway *g_gateway = nullptr;

static void print_trades(const std::optional<std::vector<Trade>> &result)
{
    if (!result)
    {
        std::cout << "  rejected\n";
        return;
    }
    if (result->empty())
        std::cout << "  rested on book\n";
    for (const Trade &t : *result)
        std::cout << "  trade: taker=" << t.taker_id << " maker=" << t.maker_id
                  << " price=" << t.price << " qty=" << t.quantity << "\n";
}

static int demo()
{
    // prices in cents; orders must be 1..1000 shares priced $10.00..$500.00
    TradeExecution exec(RiskLimits{1000, 1000, 50000});

    std::cout << "sell 300 @ $50.00\n";
    print_trades(exec.execute(1, Side::Sell, 5000, 300));

    std::cout << "sell 200 @ $50.50\n";
    print_trades(exec.execute(2, Side::Sell, 5050, 200));

    std::cout << "buy 400 @ $50.50 (sweeps two levels)\n";
    print_trades(exec.execute(3, Side::Buy, 5050, 400));

    std::cout << "buy 1500 @ $50.00 (exceeds max order size)\n";
    print_trades(exec.execute(4, Side::Buy, 5000, 1500));

    const OrderBook &book = exec.engine().book();
    std::cout << "resting orders: " << book.order_count() << "\n";
    if (auto ask = book.best_ask())
        std::cout << "best ask: " << *ask << " (qty " << book.depth_at(Side::Sell, *ask) << ")\n";
    return 0;
}

static int serve(std::uint16_t port, const std::string &host)
{
    TradeExecution exec(RiskLimits{1000, 1000, 50000});
    TcpGateway gateway(exec);
    if (!gateway.listen(port, host))
    {
        std::cerr << "could not listen on " << host << ":" << port << "\n";
        return 1;
    }

    g_gateway = &gateway;
    std::signal(SIGINT, [](int) { g_gateway->stop(); });
    std::signal(SIGTERM, [](int) { g_gateway->stop(); });

    std::cout << "listening on " << host << ":" << gateway.port()
              << " (risk limits: size <= 1000, price 1000..50000 ticks); ctrl-c to stop\n";
    gateway.run();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "serve") == 0)
        return serve(argc >= 3 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 9000, argc >= 4 ? argv[3] : "127.0.0.1");
    if (argc == 1 || std::strcmp(argv[1], "demo") == 0)
        return demo();

    std::cerr << "usage: trading_system [demo | serve [port [host]]]\n";
    return 2;
}
