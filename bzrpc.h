#ifndef __BZRPC_H__
#define __BZRPC_H__

#include <stdint.h>
#include <sys/queue.h>


#define BZRPC_LOG_INFO(fmt, ...)        printf("[INFO] "fmt"    @[%s:%d]\n", ##__VA_ARGS__, __func__, __LINE__)
#define BZRPC_LOG_ERR(fmt, ...)         printf("[ERR ] "fmt"    @[%s:%d]\n", ##__VA_ARGS__, __func__, __LINE__)
#define BZRPC_LOG_WARN(fmt, ...)        printf("[WARN] "fmt"    @[%s:%d]\n", ##__VA_ARGS__, __func__, __LINE__)
#define BZRPC_LOG_DEBUG(fmt, ...)       printf("[DBG ] "fmt"    @[%s:%d]\n", ##__VA_ARGS__, __func__, __LINE__)


typedef enum bzrpc_errno{
	BZRPC_ERR_NONE = 0,
	BZRPC_ERR_FAIL,
	BZRPC_ERR_BADMSG,
	BZRPC_ERR_MEM,
	BZRPC_ERR_SOCK,
	BZRPC_ERR_INVALID,
	BZRPC_ERR_BADPARAM,
	BZRPC_ERR_DECODE,
	BZRPC_ERR_ENCODE,
	BZRPC_ERR_NOTFOUND,
	BZRPC_ERR_TIMEOUT,
}bzrpc_errno_t;

#define BZRPC_INVALID_ID (~0)


#define BZRPC_DEBUG_REQ 0x1
#define BZRPC_DEBUG_RES 0X2


typedef struct bzrpc_ctx bzrpc_ctx_t;




#define  BZRPC_RET_SUCESS 0
#define  BZRPC_RET_ACCEPT 1
#define  BZRPC_RET_REJECT 2

/*
* if return BZRPC_RET_SUCESS, rpc server will send response with ctx
* if return BZRPC_RET_ACCEPT, rpc server will not send response
* if return BZRPC_RET_REJECT, rpc server just warning a request was rejected
*/
typedef int bzrpc_func_t(bzrpc_ctx_t *ctx);


struct bzrpc_procedure{
	char *method;   /*index of procedure*/
	int multithread;
	bzrpc_func_t *func;
	TAILQ_ENTRY(bzrpc_procedure) list;
};



struct bzrpc_server;


/*This a callback invoked when server accept a new connection
* @server: RPC server
* @sockfd: new socket fd created when server accept a new connection
* @close:  whether close the sockfd after finish to use the connection
* Return:  return 0 if success and socket will be closed if close=1, otherwise socket will not be closed.
*/
typedef int bzrpc_accept_t(struct bzrpc_server *server, int sockfd, int close);

#define BZRPC_METHOD_HASH_SIZE 256
struct bzrpc_server{
	char *name;
	char *addr;
	char *spec;
	void *data;
	int timeout;
	int debug;
 	int (*start)(struct bzrpc_server *, bzrpc_accept_t *accept);
	int (*stop)(struct bzrpc_server *);
	int (*status)(struct bzrpc_server *);
	int (*new_ctx)(struct bzrpc_server *, bzrpc_ctx_t **ctx);
	struct bzrpc_transport *transport;
	TAILQ_HEAD(, bzrpc_procedure) procedure_list[BZRPC_METHOD_HASH_SIZE];
};



struct bzrpc_conn{
	void *data;
	int (*set_timeout)(struct bzrpc_conn *conn, int ms);
	int (*send)(struct bzrpc_conn *conn, uint8_t *data, int len);
	int (*recv)(struct bzrpc_conn *conn, uint8_t **data, int *len);
	struct bzrpc_transport *transport;
	
};

/*bzrpc transport*/
struct bzrpc_transport{
	const char *name;
	int (*setup_server)(struct bzrpc_server *server, const char *addr);
	void (*clear_server)(struct bzrpc_server *server);
	int (*new_conn)(int sockfd, void *sockaddr, struct bzrpc_conn **conn);
	void (*destroy_conn)(struct bzrpc_conn *conn);
	void (*close_conn)(struct bzrpc_conn *conn);
	int (*new_client)(const char *addr, struct bzrpc_conn **conn);
	void (*destroy_client)(struct bzrpc_conn *conn);
	TAILQ_ENTRY(bzrpc_transport) list;
};


typedef void bzrpc_printf_t(void *cookie, const char *fmt, ...);

typedef struct bzrpc_data{
	void *addr;
	int len;
}bzrpc_data_t;


struct bzrpc_spec{
	const char *name;
	int        id;
	int (*ctx_new)(void **ctx);
	void (*ctx_destroy)(void *ctx);
	void (*ctx_dump)(void *ctx, bzrpc_printf_t *p, void *cookie);
	int (*id_get)(void *ctx);
	int (*id_set)(void *, int id);
	int (*method_get)(void *ctx, char **method);
	int (*method_set)(void *ctx, const char *method);
	int (*encode_req)(void *, uint8_t **, int *);
	int (*decode_req)(void *, uint8_t *, int);
	int (*encode_resp)(void *, uint8_t **, int *);
	int (*decode_resp)(void *, uint8_t *, int);
	int (*param_add)(void *, const char *, void *);
	int (*param_add_str)(void *, const char *, const char *);
	int (*param_get)(void *, const char *, void **);
	int (*param_get_byindex)(void *, int, char **);
	int  (*param_size_get)(void *);
	void (*param_clear)(void *);
	int (*result_add)(void*, void *);
	int (*result_add_str)(void *, const char *);
	int (*result_get)(void*, void **);
	int (*result_get_byindex)(void *, int, void **);
	int  (*result_size_get)(void *);
	void (*result_clear)(void *);
	int (*error_code_set)(void *, int);
	int (*error_code_get)(void *);
	int  (*error_msg_set)(void *, const char *);
	int (*error_msg_get)(void*, char **);
	TAILQ_ENTRY(bzrpc_spec) list;
};



void *bzrpc_mem_alloc(int size);
void bzrpc_mem_free(void *p);
void bzrpc_mem_safe_free(void **p);

#define BZRPC_MEM_SAFE_FREE(pp) bzrpc_mem_safe_free((void **)(pp))


void bzrpc_ctx_dump(bzrpc_ctx_t *ctx, bzrpc_printf_t *p, void *cookie);



bzrpc_ctx_t *bzrpc_ctx_new(const char *spec);

void bzrpc_set_debug_flag(bzrpc_ctx_t *ctx, int flag);

void bzrpc_ctx_destroy(bzrpc_ctx_t *ctx);
int bzrpc_id_get(bzrpc_ctx_t *ctx);
int bzrpc_id_set(bzrpc_ctx_t *ctx, int id);
int bzrpc_param_get(bzrpc_ctx_t *ctx, const char *name, void **value);
#define BZRPC_PARAM_GET(ctx, name) \
	({\
		void *_p; \
		bzrpc_param_get(ctx, name, &_p) ? NULL : _p; \
	})


int bzrpc_param_add(bzrpc_ctx_t *ctx, const char *name, void *value);
int bzrpc_param_add_str(bzrpc_ctx_t *ctx, const char *name, const char *value);

void bzrpc_param_clear(bzrpc_ctx_t *ctx);
void bzrpc_result_clear(bzrpc_ctx_t *ctx);
int bzrpc_result_add(bzrpc_ctx_t *ctx, void *result);
int bzrpc_result_add_str(bzrpc_ctx_t *ctx, const char *result);
int bzrpc_result_get(bzrpc_ctx_t *ctx, void **result);
#define BZRPC_RESULT_GET(ctx) \
	({\
		void *_p; \
		bzrpc_result_get(ctx, &_p) ? NULL : _p; \
	})

int bzrpc_error_code_get(bzrpc_ctx_t *ctx);
int bzrpc_error_code_set(bzrpc_ctx_t *ctx, int code);
int bzrpc_error_msg_set(bzrpc_ctx_t *ctx, const char *msg);
int bzrpc_error_msg_get(bzrpc_ctx_t *ctx, char **msg);
#define BZRPC_ERROR_MSG_GET(ctx) \
	({\
		char *_s; \
		bzrpc_error_msg_get(ctx, &_s) ? NULL : _s; \
	})


int bzrpc_method_get(bzrpc_ctx_t *ctx, char **method);
#define BZRPC_METHOD_GET(ctx) \
	({\
		char *_s; \
		bzrpc_method_get(ctx, &_s) ? NULL : _s; \
	})

int bzrpc_method_set(bzrpc_ctx_t *ctx, const char *method);

int bzrpc_encode_request(bzrpc_ctx_t *ctx, uint8_t **data, int *len);
int bzrpc_encode_request(bzrpc_ctx_t *ctx, uint8_t **data, int *len);
int bzrpc_encode_response(bzrpc_ctx_t *ctx, uint8_t **data, int *len);
int bzrpc_decode_response(bzrpc_ctx_t *ctx, uint8_t *data, int len);



struct bzrpc_conn  *bzrpc_new_conn(const char *transport, int sockfd, void *peer_sockaddr);
void bzrpc_destroy_conn(struct bzrpc_conn *conn);
struct bzrpc_conn  *bzrpc_new_client(const char *addr);
void bzrpc_destroy_client(struct bzrpc_conn *conn);




int bzrpc_request(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn);
int bzrpc_reply(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn);

int bzrpc_call(bzrpc_ctx_t *ctx, const char *addr);

int bzrpc_fcall(bzrpc_ctx_t **ctx, const char *addr, const char *method, const char *params, int id);



int bzrpc_recv_response(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn);
int bzrpc_recv_request(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn);

int bzrpc_addr_resolve(const char *addr, char **transport, char **host, uint16_t *port);


int bzrpc_conn_set_timeout(struct bzrpc_conn *conn, int timeout);


struct bzrpc_server *bzrpc_server_new(const char *name, const char *addr);
void bzrpc_server_destroy(struct bzrpc_server *server);
int bzrpc_server_start(struct bzrpc_server *server);
int bzrpc_server_stop(struct bzrpc_server *server);
int bzrpc_server_accept(struct bzrpc_server *server, int sockfd, int close);
int bzrpc_register_procedure(struct bzrpc_server *server, const char *method, bzrpc_func_t *func, int multithread);



#endif


