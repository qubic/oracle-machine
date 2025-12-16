#include "om_network/session.h"
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <mstcpip.h>
#define SHUT_RDWR SD_BOTH
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>

namespace oracle
{

Session::Session(int socketFD, const std::string& remoteIP, uint16_t remotePort) :
    _socketFD(socketFD),
    _remoteIP(remoteIP),
    _remotePort(remotePort),
    _active(socketFD >= 0),
    _bytesSent(0),
    _bytesReceived(0)
{
}

Session::~Session()
{
    close();
}

// Move Constructor
Session::Session(Session&& other) noexcept :
    _socketFD(other._socketFD),
    _remoteIP(std::move(other._remoteIP)),
    _remotePort(other._remotePort),
    _active(other._active.load()),
    _bytesSent(other._bytesSent),
    _bytesReceived(other._bytesReceived)
{
    // Invalidate source
    other._socketFD = -1;
    other._active.store(false);
}

// Move Assignment
Session& Session::operator=(Session&& other) noexcept
{
    if (this != &other)
    {
        // Close current resource
        close();

        // Steal resources
        _socketFD = other._socketFD;
        _remoteIP = std::move(other._remoteIP);
        _remotePort = other._remotePort;
        _active.store(other._active.load());
        _bytesSent = other._bytesSent;
        _bytesReceived = other._bytesReceived;

        // Invalidate source
        other._socketFD = -1;
        other._active.store(false);
    }
    return *this;
}

void Session::close()
{
    if (_socketFD >= 0)
    {
        // Only attempt shutdown if we consider the connection active
        // otherwise it might be already broken
        if (_active.load())
        {
            shutdown(_socketFD, SHUT_RDWR);
        }
#ifdef _MSC_VER
        closesocket(_socketFD);
#else
        ::close(_socketFD);
#endif

        std::cout << "Session closed: " << _remoteIP << ":" << _remotePort << " [FD:" << _socketFD
                  << "]" << std::endl;

        _socketFD = -1;
        _active.store(false);
    }
}

bool Session::setTimeout(uint32_t timeout_ms)
{
    if (_socketFD < 0)
        return false;

#ifdef _MSC_VER
    DWORD timeout = timeout_ms;
    int ret_recv =
        setsockopt(_socketFD, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    int ret_send =
        setsockopt(_socketFD, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret_recv = setsockopt(_socketFD, SOL_SOCKET, SO_RCVTIMEO, (const void*)&tv, sizeof(tv));
    int ret_send = setsockopt(_socketFD, SOL_SOCKET, SO_SNDTIMEO, (const void*)&tv, sizeof(tv));
#endif

    return (ret_recv == 0 && ret_send == 0);
}

bool Session::sendData(const uint8_t* data, int size)
{
    if (!_active || size <= 0 || data == nullptr || _socketFD < 0)
        return false;

    int total_sent = 0;
    while (total_sent < size)
    {
        // On Linux/Mac, standard send() can trigger SIGPIPE if socket is closed.
        // pass MSG_NOSIGNAL to prevent app crash. Windows ignores this flag.
#ifdef _MSC_VER
        int flags = 0;
#else
        int flags = MSG_NOSIGNAL;
#endif
        int sent = send(_socketFD, (const char*)data + total_sent, size - total_sent, flags);
        if (sent <= 0)
        {
            _active.store(false);
            return false;
        }
        total_sent += sent;
        _bytesSent += sent;
    }
    return true;
}

int Session::receiveExact(uint8_t* buffer, int sz)
{
    if (!_active || _socketFD < 0)
        return 0;

    int total_received = 0;
    while (sz > 0)
    {
        int received = recv(_socketFD, (char*)buffer + total_received, sz, 0);
        if (received <= 0)
        {
            // 0 = Graceful close, -1 = Error (or timeout)
            _active.store(false);
            break;
        }
        total_received += received;
        _bytesReceived += received;
        sz -= received;
    }
    return total_received;
}

int Session::receive(uint8_t* buffer, int sz)
{
    if (!_active || _socketFD < 0)
        return 0;

    int received = recv(_socketFD, (char*)buffer, sz, 0);
    if (received <= 0)
    {
        _active.store(false);
        return 0;
    }

    _bytesReceived += received;
    return received;
}

void Session::forceShutdown()
{
    // Simply shutdown logic, close() will be called by owner or destructor
    if (_socketFD >= 0 && _active.load())
    {
        shutdown(_socketFD, SHUT_RDWR); // this will unblock recv()/send()
        _active.store(false);
    }
}

void Session::setKeepAlive(bool enable, int idleSec, int intervalSec, int count)
{
    if (!enable)
        return;

    if (_socketFD < 0)
        return;

    int optval = 1;
    setsockopt(_socketFD, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, sizeof(optval));
#ifdef _MSC_VER
    tcp_keepalive settings;
    settings.onoff = 1;
    settings.keepalivetime = idleSec * 1000;
    settings.keepaliveinterval = intervalSec * 1000;

    DWORD bytes_returned;
    WSAIoctl(
        _socketFD,
        SIO_KEEPALIVE_VALS,
        &settings,
        sizeof(settings),
        NULL,
        0,
        &bytes_returned,
        NULL,
        NULL);
#else
    setsockopt(_socketFD, IPPROTO_TCP, TCP_KEEPIDLE, &idleSec, sizeof(idleSec));
    setsockopt(_socketFD, IPPROTO_TCP, TCP_KEEPINTVL, &intervalSec, sizeof(intervalSec));
    setsockopt(_socketFD, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

#endif
}

} // namespace oracle