#include "tcp_gateway.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include "core/command.hpp"

namespace
{
constexpr std::size_t kMaxLineLength = 256;
constexpr std::size_t kMaxPendingOutput = 1 << 20; // drop clients that never read their data

bool make_non_blocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
} // namespace

TcpGateway::TcpGateway(TradeExecution &execution) : execution_(execution) {}

TcpGateway::~TcpGateway()
{
    for (Client &client : clients_)
        close(client.fd);
    if (listen_fd_ >= 0)
        close(listen_fd_);
}

bool TcpGateway::listen(std::uint16_t port, const std::string &host)
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        return false;

    const int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
        bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd_, 16) != 0 || !make_non_blocking(listen_fd_))
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    socklen_t len = sizeof(addr);
    getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    return true;
}

void TcpGateway::accept_clients()
{
    while (true)
    {
        const int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0)
            return; // EAGAIN: nothing more to accept
        if (!make_non_blocking(fd))
        {
            close(fd);
            continue;
        }
#ifdef SO_NOSIGPIPE
        const int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
        clients_.push_back(Client{fd, {}, {}, false});
    }
}

void TcpGateway::flush(Client &client)
{
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    while (!client.out.empty())
    {
        const ssize_t n = send(client.fd, client.out.data(), client.out.size(), flags);
        if (n > 0)
        {
            client.out.erase(0, static_cast<std::size_t>(n));
        }
        else
        {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                break; // try again when the socket is writable
            client.closing = true;
            break;
        }
    }
    if (client.out.size() > kMaxPendingOutput)
        client.closing = true;
}

void TcpGateway::broadcast(std::vector<Client> &clients, const std::string &line)
{
    // closing clients are included on purpose: a client that sent its commands and then
    // half-closed (e.g. `printf ... | nc`) still gets its replies flushed before it is dropped
    for (Client &client : clients)
        client.out += line;
}

void TcpGateway::handle_line(Client &sender, const std::string &line, std::vector<Client> &clients)
{
    const auto command = parse_command(line);
    if (!command)
    {
        // blank lines and comments are ignored; anything else non-parseable is an error
        if (line.find_first_not_of(" \t\r") != std::string::npos && line[line.find_first_not_of(" \t\r")] != '#')
            sender.out += "ERROR MALFORMED\n";
        return;
    }

    trades_.clear();
    const OrderId id = command->order.id;
    Status status = Status::Ok;
    switch (command->kind)
    {
    case Command::Kind::New:
        status = execution_.submit(command->order, trades_);
        break;
    case Command::Kind::Cancel:
        status = execution_.cancel(id) ? Status::Ok : Status::NotFound;
        break;
    case Command::Kind::Modify:
        status = execution_.modify(id, command->order.price, command->order.quantity, trades_);
        break;
    }

    if (status != Status::Ok)
    {
        sender.out += "REJECT " + std::to_string(id) + " " + to_string(status) + "\n";
        return;
    }

    sender.out += "ACK " + std::to_string(id) + "\n";
    for (const Trade &t : trades_)
    {
        broadcast(clients, "TRADE " + std::to_string(t.taker_id) + " " + std::to_string(t.maker_id) + " " +
                               std::to_string(t.price) + " " + std::to_string(t.quantity) + "\n");
    }
}

void TcpGateway::read_client(Client &client, std::vector<Client> &clients)
{
    char buffer[4096];
    while (true)
    {
        const ssize_t n = recv(client.fd, buffer, sizeof(buffer), 0);
        if (n > 0)
        {
            client.in.append(buffer, static_cast<std::size_t>(n));
        }
        else
        {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                break;
            client.closing = true; // orderly shutdown (n == 0) or error
            break;
        }
    }

    std::size_t start = 0;
    for (std::size_t end; (end = client.in.find('\n', start)) != std::string::npos; start = end + 1)
        handle_line(client, client.in.substr(start, end - start), clients);
    client.in.erase(0, start);

    if (client.in.size() > kMaxLineLength)
    {
        client.out += "ERROR LINE_TOO_LONG\n";
        client.closing = true;
    }
}

void TcpGateway::run()
{
    while (!stop_requested_)
    {
        std::vector<pollfd> fds;
        fds.push_back(pollfd{listen_fd_, POLLIN, 0});
        for (const Client &client : clients_)
            fds.push_back(pollfd{client.fd, static_cast<short>(POLLIN | (client.out.empty() ? 0 : POLLOUT)), 0});

        if (poll(fds.data(), fds.size(), 50) < 0 && errno != EINTR)
            return;

        // fds[i + 1] belongs to clients_[i]; accepting appends, so do it after reading
        const std::size_t existing = clients_.size();
        for (std::size_t i = 0; i < existing; ++i)
        {
            if (fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR))
                read_client(clients_[i], clients_);
        }
        if (fds[0].revents & POLLIN)
            accept_clients();

        for (Client &client : clients_)
            flush(client);

        for (auto it = clients_.begin(); it != clients_.end();)
        {
            if (it->closing)
            {
                close(it->fd);
                it = clients_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
