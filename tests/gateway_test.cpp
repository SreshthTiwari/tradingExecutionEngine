// End-to-end test of the TCP gateway: a real server thread on an ephemeral loopback port
// and real client sockets.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include "network/tcp_gateway.hpp"
#include "test_util.hpp"

namespace
{
class Client
{
public:
    explicit Client(std::uint16_t port)
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        CHECK(connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
        timeval timeout{3, 0};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    ~Client() { close(fd_); }

    void send(const std::string &text) const { CHECK(::send(fd_, text.data(), text.size(), 0) == (ssize_t)text.size()); }

    // next line from the server, or "<timeout>" if nothing arrives in time
    std::string read_line()
    {
        while (true)
        {
            const auto end = buffer_.find('\n');
            if (end != std::string::npos)
            {
                std::string line = buffer_.substr(0, end);
                buffer_.erase(0, end + 1);
                return line;
            }
            char chunk[512];
            const ssize_t n = recv(fd_, chunk, sizeof(chunk), 0);
            if (n <= 0)
                return "<timeout>";
            buffer_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    // true if the server closed the connection
    bool closed_by_server()
    {
        char c;
        return recv(fd_, &c, 1, 0) == 0;
    }

    int fd() const { return fd_; }

private:
    int fd_ = -1;
    std::string buffer_;
};
} // namespace

int main()
{
    TradeExecution execution(RiskLimits{1000, 1, 100000});
    TcpGateway gateway(execution);
    CHECK(gateway.listen(0));
    CHECK(gateway.port() != 0);
    std::thread server([&] { gateway.run(); });

    {
        Client a(gateway.port());
        Client b(gateway.port());

        a.send("NEW 1 S 100 10\n");
        CHECK(a.read_line() == "ACK 1");

        // a command split across two packets still parses
        b.send("NEW 2 B 1");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        b.send("00 4\n");
        CHECK(b.read_line() == "ACK 2");
        CHECK(b.read_line() == "TRADE 2 1 100 4"); // the trade is broadcast to everyone
        CHECK(a.read_line() == "TRADE 2 1 100 4");

        // market order, modify, cancel
        b.send("MKT 3 B 6\n");
        CHECK(b.read_line() == "ACK 3");
        CHECK(b.read_line() == "TRADE 3 1 100 6");
        CHECK(a.read_line() == "TRADE 3 1 100 6");
        b.send("NEW 4 B 90 5\nMODIFY 4 91 5\nCANCEL 4\nCANCEL 4\n"); // several commands in one packet
        CHECK(b.read_line() == "ACK 4");
        CHECK(b.read_line() == "ACK 4");
        CHECK(b.read_line() == "ACK 4");
        CHECK(b.read_line() == "REJECT 4 NOT_FOUND");

        // rejections and errors
        a.send("NEW 5 B 100 5000\n");
        CHECK(a.read_line() == "REJECT 5 RISK_REJECTED");
        a.send("NEW 6 B 100 5 FOK\n");
        CHECK(a.read_line() == "REJECT 6 NOT_FILLABLE");
        a.send("NEW 6 B 100\n");
        CHECK(a.read_line() == "ERROR MALFORMED");
        a.send("\n# just a comment\n");
        a.send("CANCEL 777\n"); // blank/comment lines produce no reply, so this is the next line
        CHECK(a.read_line() == "REJECT 777 NOT_FOUND");

        // a client that disconnects abruptly does not take the server down
        {
            Client gone(gateway.port());
            gone.send("NEW 20 S 500 1\n");
            CHECK(gone.read_line() == "ACK 20");
        }
        b.send("NEW 21 B 500 1\n");
        CHECK(b.read_line() == "ACK 21");
        CHECK(b.read_line() == "TRADE 21 20 500 1");

        // a client that half-closes right after sending (like `printf ... | nc`) still gets
        // its ACK and the trades it caused
        {
            Client oneshot(gateway.port());
            oneshot.send("NEW 30 S 700 2\nMKT 31 B 2\n");
            shutdown(oneshot.fd(), SHUT_WR);
            CHECK(oneshot.read_line() == "ACK 30");
            CHECK(oneshot.read_line() == "ACK 31");
            CHECK(oneshot.read_line() == "TRADE 31 30 700 2");
            CHECK(oneshot.closed_by_server());
        }

        // an endless line without a newline gets the client dropped
        Client abusive(gateway.port());
        abusive.send(std::string(1000, 'x'));
        CHECK(abusive.read_line() == "ERROR LINE_TOO_LONG");
        CHECK(abusive.closed_by_server());
    }

    gateway.stop();
    server.join();

    CHECK(execution.engine().book().order_count() == 0); // everything was filled or cancelled
    return TEST_MAIN_RESULT();
}
