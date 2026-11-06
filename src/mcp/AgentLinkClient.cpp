// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/Command.h"
#include "mcp/AgentLink.h"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kBad = INVALID_SOCKET;
static void closeSocket(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kBad = -1;
static void closeSocket(socket_t s) { ::close(s); }
#endif

namespace orv {

struct AgentLinkClient::Impl {
    socket_t s{kBad};
    std::string buffer;

    ~Impl() {
        if (s != kBad) closeSocket(s);
#ifdef _WIN32
        WSACleanup();
#endif
    }
    void sendLine(const std::string& line) {
        std::string data = line + "\n";
        std::size_t sent = 0;
        while (sent < data.size()) {
            const auto n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
            if (n <= 0) throw CommandError("unavailable", "The OpenRevers app closed the connection.");
            sent += static_cast<std::size_t>(n);
        }
    }
    std::string readLine() {
        for (;;) {
            const auto nl = buffer.find('\n');
            if (nl != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                return line;
            }
            char chunk[65536];
            const auto n = ::recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0) throw CommandError("unavailable", "The OpenRevers app closed the connection (was it closed?).");
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
    }
};

std::unique_ptr<AgentLinkClient> AgentLinkClient::connect(int port, std::string token, std::string lang) {
    auto impl = std::make_unique<Impl>();
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return nullptr;
#endif
    impl->s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl->s == kBad) return nullptr;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(impl->s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return nullptr;
    int one = 1;
    setsockopt(impl->s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    std::unique_ptr<AgentLinkClient> c(new AgentLinkClient());
    c->impl_ = std::move(impl);
    c->token_ = std::move(token);
    c->lang_ = std::move(lang);
    try {
        c->request({{"op", "hello"}});
    } catch (const std::exception&) {
        return nullptr;  // something else listens there, or the token is wrong
    }
    return c;
}

AgentLinkClient::~AgentLinkClient() = default;

json AgentLinkClient::request(json req) {
    req["token"] = token_;
    req["id"] = nextId_++;
    req["lang"] = lang_;
    impl_->sendLine(req.dump());
    const json resp = json::parse(impl_->readLine());
    if (!resp.value("ok", false)) throw CommandError(resp.value("code", "failed"), resp.value("message", "The app refused the request."));
    return resp.value("result", json());
}

json AgentLinkClient::listCommands() { return request({{"op", "list"}}); }

json AgentLinkClient::call(const std::string& name, const json& params) {
    return request({{"op", "call"}, {"name", name}, {"params", params}});
}

}  // namespace orv
