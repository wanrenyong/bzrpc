#ifndef __BZRPC_UDP_H__
#define __BZRPC_UDP_H__

int bzrpc_udp_conn_new(int sockfd, void *peer, struct bzrpc_conn **conn);
void bzrpc_udp_conn_destroy(struct bzrpc_conn *conn);
int bzrpc_udp_client_new(const char *addr, struct bzrpc_conn **conn);
void bzrpc_udp_client_destroy(struct bzrpc_conn *conn);
void bzrpc_udp_conn_close(struct bzrpc_conn *conn);



int bzrpc_udp_server_setup(struct bzrpc_server *server, const char *addr);
void bzrpc_udp_server_clear(struct bzrpc_server *server);





#endif

