#ifndef TCP_GATEWAY_HPP
#define TCP_GATEWAY_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/trade_execution.hpp"

// Line-based TCP order gateway (POSIX only). One thread multiplexes all clients with poll(),
// so the engine itself needs no locking.
//
// Clients send the commands documented in core/command.hpp, one per line. The server answers
// the sender with
//   ACK <id>                    command accepted
//   REJECT <id> <STATUS>        e.g. RISK_REJECTED, NOT_FOUND, NOT_FILLABLE (see Status)
//   ERROR MALFORMED             line could not be parsed
// and broadcasts every execution to all connected clients:
//   TRADE <taker_id> <maker_id> <price> <qty>
//
// This is a plain TCP protocol, not WebSocket. There is no authentication, and order ids share
// one namespace across all clients.
class TcpGateway
{
public:
    explicit TcpGateway(TradeExecution &execution);
    ~TcpGateway();

    TcpGateway(const TcpGateway &) = delete;
    TcpGateway &operator=(const TcpGateway &) = delete;

    // binds and listens; port 0 picks a free port (see port()). Returns false on failure
    bool listen(std::uint16_t port, const std::string &host = "127.0.0.1");

    std::uint16_t port() const { return port_; }

    // serves clients until stop() is called; blocks the calling thread
    void run();

    // safe to call from another thread or a signal handler
    void stop() { stop_requested_ = true; }

private:
    struct Client
    {
        int fd;
        std::string in;
        std::string out;
        bool closing = false;
    };

    void accept_clients();
    void read_client(Client &client, std::vector<Client> &clients);
    void handle_line(Client &sender, const std::string &line, std::vector<Client> &clients);
    void flush(Client &client);
    static void broadcast(std::vector<Client> &clients, const std::string &line);

    TradeExecution &execution_;
    int listen_fd_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_requested_{false};
    std::vector<Client> clients_;
    std::vector<Trade> trades_; // reused scratch buffer
};

#endif // TCP_GATEWAY_HPP
