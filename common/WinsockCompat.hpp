#pragma once

// Minimal Winsock-to-POSIX shim so the (Windows-authored) socket code in
// OSCManager compiles and runs on the Linux development build. Only the names
// the codebase actually uses are mapped. Behavioural differences that can't be
// papered over with names (SO_RCVTIMEO argument type) are handled with #ifdef
// at the call site.

#ifndef _WIN32

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

typedef int SOCKET;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

inline int closesocket(int s) { return ::close(s); }

// Winsock startup/teardown are no-ops on POSIX.
typedef int WSADATA;
inline int WSAStartup(unsigned short, WSADATA*) { return 0; }
inline int WSACleanup() { return 0; }
#ifndef MAKEWORD
#define MAKEWORD(a, b) 0
#endif

inline int WSAGetLastError() { return errno; }

#ifndef ZeroMemory
#define ZeroMemory(ptr, len) memset((ptr), 0, (len))
#endif

// The codebase checks these two Winsock error codes; map to POSIX equivalents.
#ifndef WSAETIMEDOUT
#define WSAETIMEDOUT EWOULDBLOCK
#endif
#ifndef WSAEMSGSIZE
#define WSAEMSGSIZE EMSGSIZE
#endif

// shutdown() "how" constants (Winsock names -> POSIX names).
#ifndef SD_RECEIVE
#define SD_RECEIVE SHUT_RD
#endif
#ifndef SD_SEND
#define SD_SEND SHUT_WR
#endif
#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

#endif // !_WIN32
