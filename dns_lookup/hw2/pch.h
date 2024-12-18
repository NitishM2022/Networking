// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

// add headers that you want to pre-compile here
#include <winsock2.h>
#include <winsock.h>
#include <ws2tcpip.h>

#include <minwindef.h> 
#pragma comment(lib, "Ws2_32.lib")

#include <iostream>
#include <string>
#include <sstream>   
#include <cstring>
#include <cstdlib>
#include <ctime>

#include "Socket.h"

#endif //PCH_H
