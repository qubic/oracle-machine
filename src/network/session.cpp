#include "session.h"

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define close(x) closesocket(x)
#define SHUT_RDWR SD_BOTH
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>

namespace oracle
{

// Server-side constructor (when accepting connections)
Session::Session(int socket_fd, const std::string& remote_ip)
    : _socketFD(socket_fd)
    , _remoteIP(remote_ip)
    , _remotePort(0)
    , _active(true)
    , _clientSocket(false)  // Server owns cleanup
    , _bytesSent(0)
    , _bytesReceived(0)
{
}


Session::~Session()
{
    if (_clientSocket && _socketFD >= 0)
    {
        stop();
    }
}

void Session::stop()
{
    if (_socketFD >= 0 && _active)
    {
        shutdown(_socketFD, SHUT_RDWR);
        ::close(_socketFD);
        _active = false;
        
        std::cout << "Session closed: " 
                  << _remoteIP << ":" << _remotePort
                  << " (sent: " << _bytesSent << " bytes, "
                  << "received: " << _bytesReceived << " bytes)" << std::endl;
    }
}

bool Session::sendData(const uint8_t* data, int size)
{
    if (!_active || size <= 0 || data == nullptr)
        return false;

    int total_sent = 0;
    while (total_sent < size)
    {
        int sent = send(_socketFD, (const char*)data + total_sent, size - total_sent, 0);
        if (sent <= 0)
        {
            _active = false;
            return false;
        }
        total_sent += sent;
        _bytesSent += sent;
    }
    return true;
}

int Session::receiveExact(uint8_t* buffer, int sz)
{
    if (!_active)
        return 0;

    int total_received = 0;
    while (sz > 0)
    {
        int received = recv(_socketFD, (char*)buffer + total_received, sz, 0);
        if (received <= 0)
        {
            _active = false;
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
    if (!_active)
        return 0;

    int received = recv(_socketFD, (char*)buffer, sz, 0);
    if (received <= 0)
    {
        _active = false;
        return 0;
    }
    
    _bytesReceived += received;
    return received;
}

} // namespace oracle