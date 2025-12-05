#include "om_network/tcp_client.h"
#include <om_common/logger.h>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define close(x) closesocket(x)
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#endif

#include <cstring>
#include <iostream>

namespace oracle
{

TcpClient::TcpClient()
{
#ifdef _MSC_VER
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
}

TcpClient::~TcpClient()
{
#ifdef _MSC_VER
    WSACleanup();
#endif
}

bool TcpClient::setNonBlocking(int sock_fd)
{
#ifdef _MSC_VER
    u_long mode = 1;
    return (ioctlsocket(sock_fd, FIONBIO, &mode) == 0);
#else
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags == -1) return false;
    return (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == 0);
#endif
}

bool TcpClient::waitForConnect(int sock_fd, int timeout_ms)
{
    // Use poll() for both Windows (Vista+) and Linux for consistency
#ifdef _MSC_VER
    WSAPOLLFD pfd;
    pfd.fd = sock_fd;
    pfd.events = POLLWRNORM; // Equivalent to POLLOUT for connect
    pfd.revents = 0;
    int result = WSAPoll(&pfd, 1, timeout_ms);
#else
    struct pollfd pfd;
    pfd.fd = sock_fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int result = poll(&pfd, 1, timeout_ms);
#endif

    if (result <= 0) return false; // Timeout or error

    // Check for socket errors
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0)
    {
        return false;
    }
    
    // If error is 0, connection successful
    return (error == 0);
}

std::unique_ptr<Session> TcpClient::connect(
    const std::string& host,
    uint16_t port,
    int timeout_ms)
{
    struct addrinfo hints, *res, *p;
    int status;
    char port_str[6];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    
    snprintf(port_str, sizeof(port_str), "%u", port);

    // Modern DNS resolution (Thread-safe & IPv6 compatible)
    if ((status = getaddrinfo(host.c_str(), port_str, &hints, &res)) != 0)
    {
        OM_LOG_ERROR() << "getaddrinfo failed: " << gai_strerror(status) << std::endl;
        return nullptr;
    }

    int sock_fd = -1;

    // Iterate through all results (e.g. try IPv6 first, then IPv4)
    for (p = res; p != nullptr; p = p->ai_next)
    {
        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd < 0) continue;

        // Set non-blocking for timeout support
        if (!setNonBlocking(sock_fd))
        {
            ::close(sock_fd);
            sock_fd = -1;
            continue;
        }

        // Attempt connection
        int result = ::connect(sock_fd, p->ai_addr, (int)p->ai_addrlen);
        
        // Check for "In Progress"
#ifdef _MSC_VER
        int conn_errno = WSAGetLastError();
        bool in_progress = (result < 0 && conn_errno == WSAEWOULDBLOCK);
#else
        int conn_errno = errno;
        bool in_progress = (result < 0 && (conn_errno == EINPROGRESS));
#endif

        if (result == 0 || (in_progress && waitForConnect(sock_fd, timeout_ms)))
        {
            // Connection Successful!
            break; // Exit loop, keeping sock_fd valid
        }

        // Connection failed, close and try next address
        ::close(sock_fd);
        sock_fd = -1;
    }

    freeaddrinfo(res);

    if (sock_fd < 0)
    {
        OM_LOG_ERROR() << "Failed to connect to " << host << ":" << port << std::endl;
        return nullptr;
    }

    // Restore blocking mode for standard usage inside Session
#ifdef _MSC_VER
    u_long mode = 0;
    ioctlsocket(sock_fd, FIONBIO, &mode);
#else
    int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags & ~O_NONBLOCK);
#endif

    // Pass ownership of the raw socket FD to the Session
    return std::make_unique<Session>(sock_fd, host, port);
}

} // namespace oracle
