#ifndef __NETWORK_SOCKET_H__
#define __NETWORK_SOCKET_H__

typedef enum enNetworkType
{
	Network_TCP = 0,
	Network_UDP,
	Network_MAX
}NetworkType;

typedef struct stNetworkSocket
{
	NetworkType type;
	int sock_fd;
	unsigned short port;

	void *priv;
}NetworkSocket;

#ifndef CAPI
#ifdef __cplusplus
#define CAPI extern "C"
#else
#define CAPI
#endif
#endif

CAPI int getNetworkError();
CAPI int getSocketError(int sock_fd);
CAPI const char* getErrorString(int err);

CAPI NetworkSocket* NetworkSocket_Create(NetworkType type, unsigned short local_port);
CAPI void NetworkSocket_Destroy(NetworkSocket *sock);

CAPI int NetworkSocket_TryConnect(NetworkSocket *sock, const char *ip, unsigned short port, int timeout_ms);
CAPI int NetworkSocket_TryRead(NetworkSocket *sock, void *buff, int max_len, int timeout_ms);

CAPI int NetworkSocket_Connect(NetworkSocket *sock, const char *ip, unsigned short port);
CAPI int NetworkSocket_Read(NetworkSocket *sock, void *buff, int max_len);
CAPI int NetworkSocket_Write(NetworkSocket *sock, void *buff, int max_len);
CAPI void NetworkSocket_SetTimeout(NetworkSocket *sock, int timeout_ms);
CAPI int NetworkSocket_TryWrite(NetworkSocket *sock, void *buff, int max_len, int timeout_ms);

#endif
