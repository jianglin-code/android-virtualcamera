#include <assert.h>
#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <sys/time.h>

#ifdef WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#define close closesocket
static void initWinSock() {
	static int needInit = 1;
	if (needInit)
	{
		WSADATA	wsadata;
		if (WSAStartup(0x202, &wsadata) != 0) assert(0);
		needInit = 0;
	}
};



#define EWOULDBLOCK WSAEWOULDBLOCK
#define EINPROGRESS WSAEWOULDBLOCK
#define EAGAIN WSAEWOULDBLOCK

#else
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <signal.h>
#define USE_SIGNALS 1

#define initWinSock()
#define Sleep(x) usleep(x*1000)
#endif // WIN32

#include "NetworkSocket.h"

static int makeSocketNonBlocking(int sock) {
#if defined(__WIN32__) || defined(_WIN32)
	unsigned long arg = 1;
	return ioctlsocket(sock, FIONBIO, &arg) == 0;
#elif defined(VXWORKS)
	int arg = 1;
	return ioctl(sock, FIONBIO, (int)&arg) == 0;
#else
	int curFlags = fcntl(sock, F_GETFL, 0);
	return fcntl(sock, F_SETFL, curFlags|O_NONBLOCK) >= 0;
#endif
}

int makeSocketBlocking(int sock, unsigned writeTimeoutInMilliseconds) {
	int result;
#if defined(__WIN32__) || defined(_WIN32)
	unsigned long arg = 0;
	result = ioctlsocket(sock, FIONBIO, &arg) == 0;
#elif defined(VXWORKS)
	int arg = 0;
	result = ioctl(sock, FIONBIO, (int)&arg) == 0;
#else
	int curFlags = fcntl(sock, F_GETFL, 0);
	result = fcntl(sock, F_SETFL, curFlags&(~O_NONBLOCK)) >= 0;
#endif

	if (writeTimeoutInMilliseconds > 0) {
		struct timeval tv;
		tv.tv_sec = writeTimeoutInMilliseconds/1000;
		tv.tv_usec = (writeTimeoutInMilliseconds%1000)*1000;
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof tv);
	}

	return result;
}


CAPI int getNetworkError() {
#if defined(__WIN32__) || defined(_WIN32) || defined(_WIN32_WCE)
	return WSAGetLastError();
#else
	return errno;
#endif
}

CAPI int getSocketError(int sock_fd) {
	int err = 0;
	socklen_t len = sizeof(err);
	if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len) < 0) {
		err = getNetworkError();
	}
	return err;
}

CAPI const char* getErrorString(int err) {
#if defined(__WIN32__) || defined(_WIN32) || defined(_WIN32_WCE)
	static char errMsg[1000] = "\0";
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, errMsg, 1000, NULL);
	return errMsg;
#else
	return strerror(err);
#endif

}

static void ignoreSigPipeOnSocket(int socketNum) {
#ifdef USE_SIGNALS
#ifdef SO_NOSIGPIPE
    int set_option = 1;
    setsockopt(socketNum, SOL_SOCKET, SO_NOSIGPIPE, &set_option, sizeof set_option);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
#endif
}


CAPI NetworkSocket* NetworkSocket_Create(NetworkType type, unsigned short local_port) {
	int sock_fd = -1;
	NetworkSocket *sock = NULL;
	struct sockaddr_in name;
	socklen_t name_len = sizeof(name);
	int reuse_flag = 0;
	do 
	{
		initWinSock();
		sock_fd = socket(AF_INET, type==Network_TCP?SOCK_STREAM:SOCK_DGRAM, 0);
		if (sock_fd < 0) break;
		if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse_flag, sizeof(reuse_flag)) < 0) break;

		name.sin_family = AF_INET;
		name.sin_addr.s_addr = INADDR_ANY;
		name.sin_port = htons(local_port);

		if (bind(sock_fd, (struct sockaddr*)&name, name_len) != 0) break;
		if (type==Network_TCP) {
			if (!makeSocketNonBlocking(sock_fd))
                break;
            ignoreSigPipeOnSocket(sock_fd);
		}
		if (getsockname(sock_fd, (struct sockaddr*)&name, &name_len) < 0) break;

		sock = (NetworkSocket *)malloc(sizeof(NetworkSocket) + name_len);
		if (!sock) break;

		memset(sock, 0, sizeof(NetworkSocket) + name_len);
		sock->priv = sock + 1;
		sock->type = type;
		sock->port = ntohs(name.sin_port);
		sock->sock_fd = sock_fd;
	} while (0);
	
	if (!sock && sock_fd >= 0) {
		close(sock_fd);
	}

	return sock;
}

CAPI void NetworkSocket_Destroy(NetworkSocket *sock) {
	if (sock) {
		if (sock->sock_fd >= 0) close(sock->sock_fd);
		free(sock);
	}
}

CAPI int NetworkSocket_Connect(NetworkSocket *sock, const char *ip, unsigned short port) {
	struct sockaddr_in *name = NULL;
	int r = 0;
	if (sock) {
		name = (struct sockaddr_in*)sock->priv;
		name->sin_family = AF_INET;
		name->sin_port = htons(port);
		inet_pton(AF_INET, ip, &name->sin_addr);

		r = 1;
		if (sock->type == Network_TCP) {
			r = (connect(sock->sock_fd, (struct sockaddr*)name, sizeof(*name)) == 0);
			if (!r) {
				int error = getNetworkError();
				if (error == EINPROGRESS || error == EWOULDBLOCK) {
					r = 1;
				}
			}
		}
	}
	return r;
}

CAPI int NetworkSocket_TryConnect(NetworkSocket *sock, const char *ip, unsigned short port, int timeout_ms) {
	fd_set write_set, error_set;
	struct timeval tv;
	if (!NetworkSocket_Connect(sock, ip, port)) return 0;

	FD_ZERO(&write_set);
	FD_ZERO(&error_set);
	FD_SET(sock->sock_fd, &write_set);
	FD_SET(sock->sock_fd, &error_set);
	tv.tv_sec = timeout_ms/1000;
	tv.tv_usec = (timeout_ms%1000)*1000;
	if (select(sock->sock_fd+1, NULL, &write_set, &error_set, &tv) < 0) {
		int err = getNetworkError();
		getErrorString(err);
		return 0;
	}

	if (FD_ISSET(sock->sock_fd, &error_set))
		return 0;

    if (FD_ISSET(sock->sock_fd, &write_set)) {
        int err = getSocketError(sock->sock_fd);
        if (err) {
            printf("TryConnect to %s:%u failed: %s\n", ip, port, getErrorString(err));
            return 0;
        }
        return 1;
    }
	
	return 0;
}

CAPI int NetworkSocket_Read(NetworkSocket *sock, void *buff, int max_len) {
	int r = 0;
	if (sock) {
		struct sockaddr_in *name = (struct sockaddr_in*)sock->priv;
		socklen_t addr_len = sizeof(*name);
        if (sock->type == Network_TCP) {
            r = (int)recv(sock->sock_fd, buff, (size_t)max_len, 0);
        } else {
            r = (int)recvfrom(sock->sock_fd, buff, (size_t)max_len, 0, (struct sockaddr*)name, &addr_len);
        }
	}
	return r;
}

CAPI int NetworkSocket_TryRead(NetworkSocket *sock, void *buff, int max_len, int timeout_ms) {
	fd_set read_set, error_set;
	struct timeval tv;
	FD_ZERO(&read_set);
	FD_ZERO(&error_set);
	FD_SET(sock->sock_fd, &read_set);
	FD_SET(sock->sock_fd, &error_set);
	tv.tv_sec = timeout_ms/1000;
	tv.tv_usec = (timeout_ms%1000)*1000;

	if (select(sock->sock_fd+1, &read_set, NULL, &error_set, &tv) < 0) {
		int err = getNetworkError();
		getErrorString(err);
		return 0;
	}

	if (FD_ISSET(sock->sock_fd, &error_set))
		return 0;

	if (FD_ISSET(sock->sock_fd, &read_set))
		return NetworkSocket_Read(sock, buff, max_len);

	return 0;

}

CAPI int NetworkSocket_Write(NetworkSocket *sock, void *buff, int max_len) {
	int r = 0;
	if (sock) {
		struct sockaddr_in *name = (struct sockaddr_in*)sock->priv;
		socklen_t addr_len = sizeof(*name);
        if (sock->type == Network_TCP) {
            r = (int)send(sock->sock_fd, (char*)buff, (size_t)max_len, 0);
        } else {
            r = (int)sendto(sock->sock_fd, (char*)buff, (size_t)max_len, 0, (struct sockaddr*)name, addr_len);
        }
	}
	return r;
}

CAPI int NetworkSocket_TryWrite(NetworkSocket *sock, void *buff, int max_len, int timeout_ms) {
	fd_set write_set, error_set;
	struct timeval tv;
	FD_ZERO(&write_set);
	FD_ZERO(&error_set);
	FD_SET(sock->sock_fd, &write_set);
	FD_SET(sock->sock_fd, &error_set);
	tv.tv_sec = timeout_ms/1000;
	tv.tv_usec = (timeout_ms%1000)*1000;
	if (select(sock->sock_fd+1, NULL, &write_set, &error_set, &tv) < 0) {
		int err = getNetworkError();
		getErrorString(err);
		return 0;
	}

	if (FD_ISSET(sock->sock_fd, &error_set))
		return 0;

	if (FD_ISSET(sock->sock_fd, &write_set))
		return NetworkSocket_Write(sock, buff, max_len);

	return 0;
}

CAPI void NetworkSocket_SetTimeout(NetworkSocket *sock, int timeout_ms) {
	if (timeout_ms == 0)
		makeSocketNonBlocking(sock->sock_fd);
	else {
		makeSocketBlocking(sock->sock_fd, timeout_ms);
	}
}

