#include "tcp_client.h"

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
    WSAStartup(MAKEWORD(2, 0), &wsa_data);
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
    if (flags == -1)
        return false;
    return (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == 0);
#endif
}

bool TcpClient::waitForConnect(int sock_fd, int timeout_ms)
{
#ifdef _MSC_VER
    // Windows: use select()
    fd_set write_fds, error_fds;
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
    FD_SET(sock_fd, &write_fds);
    FD_SET(sock_fd, &error_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int result = select(sock_fd + 1, nullptr, &write_fds, &error_fds, &timeout);
    if (result <= 0)
    {
        // Timeout or error
        return false;
    }

    if (FD_ISSET(sock_fd, &error_fds))
    {
        // Connection error
        return false;
    }

    if (FD_ISSET(sock_fd, &write_fds))
    {
        // Check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0)
        {
            return false;
        }
        return (error == 0);
    }

    return false;
#else
    // Linux: use poll()
    struct pollfd pfd;
    pfd.fd = sock_fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    int result = poll(&pfd, 1, timeout_ms);
    if (result <= 0)
    {
        // Timeout or error
        return false;
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
    {
        // Connection error
        return false;
    }

    if (pfd.revents & POLLOUT)
    {
        // Socket is writable, check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
        {
            return false;
        }
        return (error == 0);
    }

    return false;
#endif
}

std::unique_ptr<Session> TcpClient::connect(
    const std::string& host,
    uint16_t port,
    int timeout_ms)
{
    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        std::cerr << "Failed to create socket" << std::endl;
        return nullptr;
    }

    // Resolve hostname
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Try to convert as IP address first
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0)
    {
        // Not an IP address, try DNS lookup
        struct hostent* he = gethostbyname(host.c_str());
        if (he == nullptr || he->h_addr_list[0] == nullptr)
        {
            std::cerr << "Failed to resolve hostname: " << host << std::endl;
            close(sock_fd);
            return nullptr;
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
    }

    // Set socket to non-blocking mode
    if (!setNonBlocking(sock_fd))
    {
        std::cerr << "Failed to set socket non-blocking" << std::endl;
        close(sock_fd);
        return nullptr;
    }

    // Attempt connection (will return immediately with EINPROGRESS) / Non-blocking connect
    int result = ::connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
#ifdef _MSC_VER
    int conn_errno = WSAGetLastError();
    bool in_progress = (result < 0 && conn_errno == WSAEWOULDBLOCK);
#else
    int conn_errno = errno;
    bool in_progress = (result < 0 && (conn_errno == EINPROGRESS || conn_errno == EWOULDBLOCK));
#endif

    if (result < 0 && !in_progress)
    {
        // Immediate error (not in progress)
        std::cerr << "Failed to connect to " << host << ":" << port 
                  << " (error: " << conn_errno << ")" << std::endl;
        close(sock_fd);
        return nullptr;
    }

    // If connection is in progress, wait for it to complete with timeout
    if (in_progress)
    {
        if (!waitForConnect(sock_fd, timeout_ms))
        {
            std::cerr << "Connection timeout to " << host << ":" << port 
                      << " (timeout: " << timeout_ms << "ms)" << std::endl;
            close(sock_fd);
            return nullptr;
        }
    }

    // Connection succeeded! Set socket back to blocking mode for easier recv/send
#ifdef _MSC_VER
    u_long mode = 0;
    ioctlsocket(sock_fd, FIONBIO, &mode);
#else
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(sock_fd, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif

    // Create and return Session
    return std::make_unique<Session>(sock_fd, host, port);
}

} // namespace oracle
