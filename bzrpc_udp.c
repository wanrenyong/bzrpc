#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/un.h>


#include "bzrpc.h"
#include "bzrpc_udp.h"



/*udp connection*/
struct bzrpc_udp_conn{
	struct bzrpc_conn base;
	int sockfd;
	struct sockaddr_in     peer;
	struct sockaddr_in     local;
	int timeout;  /*in us*/
};


struct bzrpc_udp_server{
	struct bzrpc_server *base;
	bzrpc_accept_t *accept;
	int sockfd;
	int timeout;
	int stop;
	pthread_t thread;
		
};


#define UDP_RPC_DEF_TIMEOUT (1000000) /*=1s*/

static int _bzrpc_udp_send(struct bzrpc_conn *conn, uint8_t *data, int len)
{
	int ret;
	struct bzrpc_udp_conn *udp_conn;

	udp_conn = (struct bzrpc_udp_conn  *)conn->data;
	ret = sendto(udp_conn->sockfd, data, len, 0, &udp_conn->peer, sizeof(udp_conn->peer));
	if(ret < 0){
		BZRPC_LOG_ERR("Failed to send RPC message, ret=%d", ret);
		return BZRPC_ERR_SOCK;
	}
	return 0;
}


static int _bzrpc_udp_recv_wait(struct bzrpc_udp_conn *conn)
{
	fd_set fdset;
	int ret;
	struct timeval tv;

	if(conn->timeout > 0)
	{
		tv.tv_sec = conn->timeout/1000000;
		tv.tv_usec = (conn->timeout)%(1000000);	
	}
	
	while(1){
		FD_ZERO(&fdset);
		FD_SET(conn->sockfd, &fdset);
		ret = select(conn->sockfd+1, &fdset, NULL, NULL, &tv);
		if(ret < 0)
		{
			BZRPC_LOG_ERR("Waiting to RPC message failed");
			return BZRPC_ERR_SOCK;
		}
		else if(ret == 0)
		{
			return BZRPC_ERR_TIMEOUT;;
		}
		if(!FD_ISSET(conn->sockfd, &fdset))
			continue;
		break;
	}
	return 0;
}



#define UDP_RPC_MSG_MAX_SZ 1500
static int _bzrpc_udp_recv(struct bzrpc_conn *conn, uint8_t **data, int *len)
{
	int sz;
	int ret;
	uint8_t *buf;
	int addrlen;
	
	struct bzrpc_udp_conn *udp_conn = conn->data;

	buf = calloc(UDP_RPC_MSG_MAX_SZ, 1);
	if(buf == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory for RPC message");
		return BZRPC_ERR_MEM;
	}
	if(udp_conn->timeout > 0)
	{
		ret = _bzrpc_udp_recv_wait(udp_conn);
		if(ret)
		{
			if(ret != BZRPC_ERR_TIMEOUT)
				BZRPC_LOG_DEBUG("Waiting to RPC message failed, ret=%d", ret);
			free(buf);
			return ret;
		}
	}

	addrlen = sizeof(udp_conn->peer);
	
	sz = recvfrom(udp_conn->sockfd, buf, UDP_RPC_MSG_MAX_SZ, 0,  &udp_conn->peer, &addrlen);
	if(sz < 0)
	{
		BZRPC_LOG_ERR("Faild to receive RPC message, ret=%d", sz);
		return BZRPC_ERR_SOCK;
	}
	*data = buf;
	if(len != NULL)
		*len = sz;
	return 0;
}

int _bzrpc_udp_conn_set_timeout(struct bzrpc_conn *conn, int timeout)
{
	struct bzrpc_udp_conn *udp_conn = conn->data;

	udp_conn->timeout = timeout;

	return 0;
}

static void bzrpc_udp_conn_init(struct bzrpc_udp_conn *conn)
{
	if(conn == NULL)
		return;
#if 0
	struct bzrpc_udp_conn *conn = calloc(sizeof(struct bzrpc_udp_conn), 1);
	if(conn == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory for udp connection");
		return NULL;
	}
#endif
	conn->sockfd = -1;
	conn->base.data = conn;
	conn->timeout = UDP_RPC_DEF_TIMEOUT;
	conn->base.recv = _bzrpc_udp_recv;
	conn->base.send = _bzrpc_udp_send;
	conn->base.set_timeout = _bzrpc_udp_conn_set_timeout;
}


int bzrpc_udp_request(bzrpc_ctx_t *ctx, struct bzrpc_udp_conn *conn)
{
	int ret;
	struct bzrpc_udp_conn *udp;
	
	if(ctx == NULL || conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	if(conn->sockfd < 0 || conn->base.data == NULL 
		|| conn->base.recv == NULL || conn->base.send == NULL)
	{
		BZRPC_LOG_ERR("RPC connection was not iniitted");
		return BZRPC_ERR_FAIL;
	}	

	return bzrpc_request(ctx, &udp->base);
}




static int _bzrpc_udp_conn_new(int sockfd, struct sockaddr_in *peer, struct bzrpc_udp_conn **conn)
{
	struct bzrpc_udp_conn *udp_conn;

	udp_conn = calloc(sizeof(struct bzrpc_udp_conn), 1);
	if(udp_conn == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory for UDP RPC connect");
		return BZRPC_ERR_MEM;
	}
	
	bzrpc_udp_conn_init(udp_conn);
	udp_conn->sockfd = sockfd;
	if(peer != NULL)
		udp_conn->peer = *peer;
	*conn = udp_conn;
	return 0;
}

int bzrpc_udp_conn_new(int sockfd, void *peer, struct bzrpc_conn **conn)
{
	int ret;
	struct bzrpc_udp_conn *udp_conn;

	if(conn == NULL)
		return BZRPC_ERR_BADPARAM;

	ret = _bzrpc_udp_conn_new(sockfd, (struct sockaddr_in *)peer, &udp_conn);
	if(!ret)
	{
		*conn = &udp_conn->base;
		return 0;
	}
	return ret;
}

void bzrpc_udp_conn_close(struct bzrpc_conn *conn)
{
	struct bzrpc_udp_conn *udp_conn;
	if(conn != NULL)
	{
		udp_conn = conn->data;
		if(udp_conn != NULL)
		{
			close(udp_conn->sockfd);
			udp_conn->sockfd = -1;
		}
	}
}


static void _bzrpc_udp_conn_destroy(struct bzrpc_udp_conn *udp_conn)
{
	if(udp_conn != NULL)
		free(udp_conn);
}


void bzrpc_udp_conn_destroy(struct bzrpc_conn *conn)
{
	if(conn != NULL)
		_bzrpc_udp_conn_destroy(conn->data);
}


/**
 * _bzrpc_udp_client_new - alloc udp connect, the differece to _bzrpc_udp_conn_new is that it will create a socket
 * @serverip[in]: ip of rpc server
 * @port[in]:udp port of rpc server
 * @conn[out]: a point of udp connection
 * Return: 0 if success, othewise fail
 *
 */
static int _bzrpc_udp_client_new(char *serverip, uint16_t port, struct bzrpc_udp_conn **conn)
{
	
	int sockfd;
	struct sockaddr_in     peer;
	struct bzrpc_udp_conn *udp_conn;
	int ret;
	
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0)
	{
		BZRPC_LOG_ERR("Could not create UDP socket for RPC");
		return BZRPC_ERR_SOCK;
	}
	
	peer.sin_family = AF_INET;
	peer.sin_port = htons(port);
	peer.sin_addr.s_addr = inet_addr(serverip);
	
	ret = _bzrpc_udp_conn_new(sockfd, &peer, &udp_conn);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not create UDP client for RPC");
		close(sockfd);
		return ret;
	}
	*conn = udp_conn;
	return 0;
}

#define UDP_RPC_PROTO "udp"

int bzrpc_udp_client_new(const char *addr, struct bzrpc_conn **conn)
{
	char *ip, *proto;
	int len;
	uint16_t port;
	int ret;
	
	struct bzrpc_udp_conn *udp_conn;

	if(addr == NULL || conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	ret = bzrpc_addr_resolve(addr, &proto, &ip, &port);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to decode address:%s", addr);
		return BZRPC_ERR_BADPARAM;
	}

	if(strncmp(UDP_RPC_PROTO, proto, strlen(UDP_RPC_PROTO)))
	{
		BZRPC_LOG_ERR("Protocol is not udp:%s", proto);
		return BZRPC_ERR_BADPARAM;
	}

	ret = _bzrpc_udp_client_new(ip, port, &udp_conn);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not alloc new client");
		return ret;
	}
	*conn = &udp_conn->base;
	return 0;
}



/**
 * _bzrpc_udp_client_destroy - destroy udp connection, the difference to _bzrpc_udp_conn_destroy is that it will close socket
 * @conn[in]: udp connection
 * Return: a point of udp connection
 *
 */
static void _bzrpc_udp_client_destroy(struct bzrpc_udp_conn *conn)
{
	if(conn != NULL)
	{
		close(conn->sockfd);
		_bzrpc_udp_conn_destroy(conn);
	}
}

void bzrpc_udp_client_destroy(struct bzrpc_conn *conn)
{
	if(conn != NULL)
		_bzrpc_udp_client_destroy((struct bzrpc_udp_conn *)(conn->data));
}


void *bzrpc_udp_server_loop(void *arg)
{
	struct bzrpc_udp_server *server = (struct bzrpc_udp_server *)arg;

	BZRPC_LOG_INFO("%s listen on %s", server->base->name, server->base->addr);
	
	while(!server->stop)
	{
		server->accept(server->base, server->sockfd, 0);
	}
	BZRPC_LOG_INFO("%s stopped", server->base->name);
}


static int bzrpc_udp_server_start(struct bzrpc_server *server, bzrpc_accept_t *accept)
{
	struct bzrpc_udp_server *udp_server;
	
	if(server == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	udp_server = (struct bzrpc_udp_server *)server->data;
	if(accept != NULL)
		udp_server->accept = accept;
	else
		udp_server->accept = bzrpc_server_accept;
	
	if(pthread_create(&udp_server->thread, NULL, bzrpc_udp_server_loop, udp_server))
	{

		BZRPC_LOG_ERR("Could not create udp RPC server thread\n");	
		return BZRPC_ERR_FAIL;
	}
	return 0;
}


static int bzrpc_udp_server_stop(struct bzrpc_server *server)
{
	/// TODO
	return 0;
}


static int bzrpc_udp_server_new_ctx(struct bzrpc_server *server, bzrpc_ctx_t **ctx)
{
	bzrpc_ctx_t *c;
	c =  bzrpc_ctx_new(server->spec);
	if(c == NULL)
	{
		BZRPC_LOG_ERR("Could not create rpc ctx");
		return BZRPC_ERR_FAIL;
	}
	*ctx = c;
	return 0;
}

int bzrpc_udp_server_setup(struct bzrpc_server *server, const char *addr)
{
	struct bzrpc_udp_server *s = NULL;
	char *ip = NULL;
	uint16_t port;
	struct sockaddr_in inaddr;
	int ret;

	if(addr == NULL || server == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	ret = bzrpc_addr_resolve(addr, NULL, &ip, &port);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not decode udp server address:%s", addr);
		return BZRPC_ERR_BADPARAM;
	}

	s = calloc(sizeof(struct bzrpc_udp_server), 1);
	if(s == NULL)
	{
		BZRPC_LOG_ERR("Could alloc memory for udp rpc server");
		ret = BZRPC_ERR_MEM;
		goto err;
	}

	s->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(s->sockfd < 0)
	{
		BZRPC_LOG_ERR("Could alloc scoket for udp rpc server");
		ret = BZRPC_ERR_SOCK;
		goto err;
	}

	memset(&inaddr, 0, sizeof(inaddr));
	inaddr.sin_family = AF_INET;
	inaddr.sin_port = htons(port);
	inaddr.sin_addr.s_addr = inet_addr(ip);
	if(bind(s->sockfd, (struct sockaddr *)&inaddr, sizeof(inaddr))){
		close(s->sockfd);
		BZRPC_LOG_ERR("Could bind scoket for udp rpc server");
		ret = BZRPC_ERR_SOCK;
		goto err;
	}
	s->base = server;
	server->data = s;
	server->timeout = UDP_RPC_DEF_TIMEOUT;
	server->start = bzrpc_udp_server_start;
	server->stop = bzrpc_udp_server_stop;
	server->new_ctx = bzrpc_udp_server_new_ctx;
	
	free(ip);
	return 0;

err:
	if(ip != NULL)
		free(ip);
	if(s != NULL)
		free(s);
	return ret;
	
}

void bzrpc_udp_server_clear(struct bzrpc_server *server)
{
	struct bzrpc_udp_server *s;

	if(server == NULL)
	{
		BZRPC_LOG_WARN("Bad parameter!");
		return;
	}
	s = (struct bzrpc_udp_server *)server->data;
	if(s == NULL)
		return;
	if(s->sockfd > 0)
		close(s->sockfd);
	free(s);
	server->data = NULL;
	
}





