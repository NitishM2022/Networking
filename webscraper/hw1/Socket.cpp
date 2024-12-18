// Nitish Malluru
// CSCE 463
// Fall 2024

#include "pch.h"
#include <iostream>
#include <cstring>

#define MAX_DOWNLOAD_TIME 10000
#define INITIAL_BUF_SIZE 4096
#define THRESHOLD 1024

using namespace std;

Socket::Socket() : sock(INVALID_SOCKET), allocatedSize(INITIAL_BUF_SIZE), curPos(0) {
    buf = new char[INITIAL_BUF_SIZE];

    FD_ZERO(&readSet);
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
}

Socket::~Socket() {
    Close();
    delete[] buf;
}

void Socket::clearBuff() {
    curPos = 0;
    allocatedSize = INITIAL_BUF_SIZE;
}

void Socket::Close() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

bool Socket::resizeBuffer() {
    int newSize = allocatedSize * 2;
    char* newBuf = new char[newSize];

    memcpy(newBuf, buf, allocatedSize);
    delete[] buf;

    buf = newBuf;
    allocatedSize = newSize;
    return true;
}

bool Socket::Read(int maxBuffSize) {
    int totalTime = 0;
    int start = GetTickCount();

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        int ret = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ret > 0) {
            int bytes = recv(sock, buf + curPos, allocatedSize - curPos, 0);
            if (bytes == SOCKET_ERROR) {
                //cout << "failed with " << WSAGetLastError() << " on recv" << endl;
                return false;
            } if (bytes == 0) {
                buf[curPos] = '\0';
                return true;
            }

            curPos += bytes;
            if (curPos > maxBuffSize) {
                //cout << "failed with exceeding max" << endl;
                return false;
            }

            if (allocatedSize - curPos < THRESHOLD) {
                resizeBuffer();
            }

            totalTime = GetTickCount() - start;
            if (totalTime > MAX_DOWNLOAD_TIME) {
                //cout << "failed with slow download" << endl;
                return false;
            }
        }
        else if (ret == 0) {
            //cout << "failed with timeout" << endl;
            return false;
        }
        else {
            //cout << "failed with " << WSAGetLastError() << endl;
            return false;
        }
    }
}

bool Socket::FindHost(string host, int port, string& ip) {
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    unsigned long addr = inet_addr(host.c_str());
    if (addr != INADDR_NONE) {
        server.sin_addr.s_addr = addr;
    }
    else {
        struct hostent* he = gethostbyname(host.c_str());
        if (he != nullptr) {
            server.sin_addr = *((struct in_addr*)he->h_addr);
        }
        else {
            //cout << "failed with " << WSAGetLastError() << endl;
            return false;
        }
    }

    ip = inet_ntoa(server.sin_addr);
    return true;
}

bool Socket::Connect() {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        //cout << "failed with " << WSAGetLastError() << endl;
        return false;
    }

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        //cout << "failed with " << WSAGetLastError() << endl;
        Close();
        return false;
    }

    //sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    //if (sock == INVALID_SOCKET) {
    //    cout << "failed with " << WSAGetLastError() << endl;
    //    return false;
    //}

    //u_long mode = 1;
    //if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
    //    Close();
    //    return false;
    //}
    //if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
    //    int error = WSAGetLastError();
    //    if (error != WSAEWOULDBLOCK) {
    //        cout << "failed with " << error << endl;
    //        Close();
    //        return false;
    //    }
    //}

    return true;
}

bool Socket::Send(string host, int port, string type, string request, string version) {
    string httpRequest = type + " " + request + " HTTP/" + version + "\r\n";
    httpRequest += "Host: " + host + "\r\n";
    httpRequest += "User-Agent: spiderman/1.2\r\n";
    httpRequest += "Connection: close\r\n\r\n";

    int sentBytes = send(sock, httpRequest.c_str(), httpRequest.length(), 0);
    if (sentBytes == SOCKET_ERROR) {
        //cout << "failed with " << WSAGetLastError() << " on send" << endl;
        return false;
    }

    return true;
}