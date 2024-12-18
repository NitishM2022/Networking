// Nitish Malluru
// CSCE 463
// Fall 2024

#ifndef SOCKET_H
#define SOCKET_H

#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

class Socket {
public:
    Socket();
    ~Socket();

    void clearBuff();
    bool Read(int maxBuffSize);
    bool FindHost(std::string host, int port, std::string& ip);
    bool Connect();
    bool Send(std::string host, int port, std::string type, std::string request, std::string version = "1.0");
    void Close();

    char* buf;          // current buffer
    int curPos;         // current position in buffer


private:
    SOCKET sock;        // socket handle
    int allocatedSize;  // bytes allocated for buf

    struct sockaddr_in server;
    fd_set readSet;
    timeval timeout;

    bool resizeBuffer();
};

#endif
