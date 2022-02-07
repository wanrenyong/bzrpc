//#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/un.h>

#include "cJSON.h"

#include "bzrpc.h"
#include "bzrpc_udp.h"





struct bzrpc_ctx{
	void                    *obj;
	int                      debug;
	struct bzrpc_spec        *spec;
	struct bzrpc_transport  *transport;
};



void *bzrpc_mem_alloc(int size)
{
	void *p = calloc(1, size);
	return p;
}

void bzrpc_mem_free(void *p)
{
	if(p)
		free(p);
}

void bzrpc_mem_safe_free(void **p)
{
	if(p != NULL && *p != NULL)
	{
		free(*p);
		*p = NULL;
	}
}


struct bzrpc_transport udp_transport ={
	.name = "udp",
	.new_conn = bzrpc_udp_conn_new,
	.destroy_conn = bzrpc_udp_conn_destroy,
	.close_conn = bzrpc_udp_conn_close,
	.new_client = bzrpc_udp_client_new,
	.destroy_client = bzrpc_udp_client_destroy,
	.setup_server = bzrpc_udp_server_setup,
	.clear_server = bzrpc_udp_server_clear,
};


static struct bzrpc_transport *bzrpc_lookup_transport(const char *transport)
{
	return &udp_transport;
}


extern struct bzrpc_spec json_rpc_spec;
extern struct bzrpc_spec raw_rpc_spec;

static struct bzrpc_spec *rpc_spec[] = {
	&json_rpc_spec,
	&raw_rpc_spec,
	NULL
};

#define NUM_RPC_SPEC  sizeof(rpc_spec)/sizeof(struct bzrpc_spec *)

static inline struct bzrpc_spec *bzrpc_spec_lookup(const char *name)
{
	int i = 0;
	struct bzrpc_spec *spec;

	if(name == NULL)
		return &json_rpc_spec;
	
	while((spec = rpc_spec[i++]) != NULL){
		if(strcmp(spec->name, name) == 0)
			return spec;
	}
	return NULL;
}


bzrpc_ctx_t *bzrpc_ctx_new(const char *spec)
{
	int ret;
	
	bzrpc_ctx_t *ctx = bzrpc_mem_alloc(sizeof(struct bzrpc_ctx));
	if(!ctx)
	{
		BZRPC_LOG_ERR("Could not alloc memory");
		return NULL;
	}

	ctx->spec = bzrpc_spec_lookup(spec);
	if(ctx->spec == NULL){
		BZRPC_LOG_ERR("Could not find RPC spec: %s", spec);
		bzrpc_mem_free(ctx);
		return NULL;
	}
	if(ctx->spec->ctx_new != NULL)
	{
		ret = ctx->spec->ctx_new(&ctx->obj);
		if(ret)
		{
			BZRPC_LOG_ERR("Could not alloc memory");
			bzrpc_mem_free(ctx);
			return NULL;
		}
	}

	return ctx;
}

static void bzrpc_printf(void *cookie, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}


void bzrpc_ctx_dump(bzrpc_ctx_t *ctx, bzrpc_printf_t *p, void *cookie)
{
	if(ctx == NULL)
		printf("Bad parameter, ctx is NULL");
	
	if(p == NULL)
		p = bzrpc_printf;
	if(ctx->spec != NULL && ctx->spec->ctx_dump != NULL)
	{
		p(cookie, "++++++++++++++++++++++++++++++++++++++++++++\n");
		ctx->spec->ctx_dump(ctx->obj, p, cookie);
		p(cookie, "++++++++++++++++++++++++++++++++++++++++++++\n");
	}
	
}


void bzrpc_ctx_destroy(bzrpc_ctx_t *ctx)
{
	if(ctx != NULL)
	{
		if(ctx->spec->ctx_destroy != NULL)
		{
			ctx->spec->ctx_destroy(ctx->obj);
			ctx->obj= NULL;
		}
		bzrpc_mem_free(ctx);	
	}
}

static int _bzrpc_new_conn(struct bzrpc_transport *transport, int sockfd, void *peer_sockaddr, struct bzrpc_conn **conn)
{
	struct bzrpc_conn *c;
	int ret;
	if(transport == NULL || conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}
	ret = transport->new_conn(sockfd, peer_sockaddr, &c);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to new rpc connection");
		return ret;
	}
	c->transport = transport;
	*conn = c;
	return 0;
}

struct bzrpc_conn  *bzrpc_new_conn(const       char *transport, int sockfd, void *peer_sockaddr)
{
	int ret;
	struct bzrpc_conn *conn;
	struct bzrpc_transport *t;
	
	if(transport == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return NULL;
	}
	t = bzrpc_lookup_transport(transport);
	ret = _bzrpc_new_conn(t, sockfd, peer_sockaddr, &conn);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not create RPC connection, ret=%d", ret);
		return NULL;
	}
	return conn;
}

void bzrpc_close_conn(struct bzrpc_conn *conn)
{
	struct bzrpc_transport *trans;
	
	if(conn == NULL)
	{
		BZRPC_LOG_WARN("Bad parameter!");
		return;
	}

	trans = conn->transport;
	
	if(trans == NULL || trans->close_conn == NULL)
	{
		BZRPC_LOG_WARN("RPC transport was not defined");
		return;
	}
	
	trans->close_conn(conn);
}

void bzrpc_destroy_conn(struct bzrpc_conn *conn)
{
	int ret;
	struct bzrpc_transport *trans;
	
	if(conn == NULL)
	{
		BZRPC_LOG_WARN("Bad parameter!");
		return;
	}
	trans = conn->transport;
	
	if(trans == NULL || trans->destroy_conn == NULL)
	{
		BZRPC_LOG_WARN("RPC transport was not defined");
		return;
	}
	
	trans->destroy_conn(conn);
}

static int _bzrpc_new_client(struct bzrpc_transport *transport, const char *addr, struct bzrpc_conn **conn)
{
	struct bzrpc_conn *c;
	int ret;
	if(transport == NULL || conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}
	ret = transport->new_client(addr, &c);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to new rpc client");
		return ret;
	}
	c->transport = transport;
	*conn = c;
	return 0;
}

struct bzrpc_conn  *bzrpc_new_client(const char *addr)
{
	int ret;
	struct bzrpc_conn *conn;
	char *s;
	struct bzrpc_transport *transport;
	
	if(addr == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return NULL;
	}

	ret = bzrpc_addr_resolve(addr, &s, NULL, NULL);
	if(ret)
	{
		BZRPC_LOG_ERR("Bad address:%s", addr);
		return NULL;
	}
	
	transport = bzrpc_lookup_transport(s);
	bzrpc_mem_free(s);
	if(transport == NULL)
	{
		BZRPC_LOG_ERR("Could not found transport:%s", s);
		return NULL;
		
	}
	ret = _bzrpc_new_client(transport, addr, &conn);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not create RPC connection, ret=%d", ret);
		return NULL;
	}
	return conn;
}


void bzrpc_destroy_client(struct bzrpc_conn *conn)
{
	int ret;
	struct bzrpc_transport *trans;
	
	if(conn == NULL)
	{
		BZRPC_LOG_WARN("Bad parameter!");
		return;
	}
	trans = conn->transport;
	
	if(trans == NULL || trans->destroy_client == NULL)
	{
		BZRPC_LOG_WARN("RPC transport was not defined");
		return;
	}
	trans->destroy_client(conn);
}


int bzrpc_conn_set_timeout(struct bzrpc_conn *conn, int timeout)
{
	if(conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(conn->set_timeout == NULL)
	{
		BZRPC_LOG_WARN("bzrpc connection set timeout was not implemented");
		return BZRPC_ERR_INVALID;
	}
	return conn->set_timeout(conn, timeout);
}


/**
 * bzrpc_get_method - Get RPC method
 * @ctx[in]: bzrpc context 
 * @method[out]: a point to return method of RPC, don't free it by yourself
 * Return: 0 when success
 *
 */

int bzrpc_method_get(bzrpc_ctx_t *ctx, char **method)
{
	char *s;
	
	if(ctx == NULL || method == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	if(ctx->spec != NULL && ctx->spec->method_get != NULL)
		return ctx->spec->method_get(ctx->obj, method);
	return BZRPC_ERR_INVALID;
	
}

/**
 * bzrpc_method_set - Set RPC method
 * @ctx[in]: bzrpc context 
 * @method[IN]: a string of RPC method
 * Return: 0 when success
 *
 */

int bzrpc_method_set(bzrpc_ctx_t *ctx, const char *method)
{
	if(ctx == NULL || method == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	if(ctx->spec != NULL && ctx->spec->method_set != NULL)
		return ctx->spec->method_set(ctx->obj, method);
	return BZRPC_ERR_INVALID;
}

/**
 * bzrpc_id_get - Get RPC id
 * @ctx[in]: bzrpc context 
 * Return: rpcid when success, BZRPC_INVALID_ID when failed
 *
 */

int bzrpc_id_get(bzrpc_ctx_t *ctx)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_INVALID_ID;
	}

	if(ctx->spec != NULL && ctx->spec->id_get != NULL)
		return ctx->spec->id_get(ctx->obj);
	return BZRPC_INVALID_ID;
}

/**
 * bzrpc_id_set - set RPC id
 * @ctx[in]: bzrpc context 
 * @id[out]: rpc id
 * Return: 0 when success
 *
 */

int bzrpc_id_set(bzrpc_ctx_t *ctx, int id)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	if(ctx->spec != NULL && ctx->spec->id_set != NULL)
		return ctx->spec->id_set(ctx->obj, id);
	return BZRPC_ERR_INVALID;;
}

/**
 * bzrpc_param_get - Get RPC parameter
 * @ctx[in]: bzrpc context 
 * @value[out]: a point to return vaule of param, don't free it by yourself
 * @name[in]: name of which param to be gotten
 * Return: 0 when success
 *
 */

int bzrpc_param_get(bzrpc_ctx_t *ctx, const char *name, void **value)
{
	if(ctx == NULL || name == NULL || value == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter");
		return BZRPC_ERR_BADPARAM;
	}

	if(ctx->spec != NULL && ctx->spec->param_get != NULL)
		return ctx->spec->param_get(ctx->obj, name, value);
	return BZRPC_ERR_INVALID;;
}


/**
 * bzrpc_param_add - add RPC parameter
 * @ctx[in]: bzrpc context 
 * @value[in]: value of param to be add
 * @name[in]: name of param to be add
 * Return: 0 when success, otherwise failed
 *
 */

int bzrpc_param_add(bzrpc_ctx_t *ctx, const char *name, void *value)
{
	if(ctx == NULL || name == NULL || value == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->param_add != NULL)
		return ctx->spec->param_add(ctx->obj, name, value);
	return BZRPC_ERR_INVALID;
}

int bzrpc_param_add_str(bzrpc_ctx_t *ctx, const char *name, const char *value)
{
	cJSON *item;
	int ret;
	int len;

	if(ctx == NULL || name == NULL || value == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->param_add_str != NULL)
		return ctx->spec->param_add_str(ctx->obj, name, value);
	
	return BZRPC_ERR_INVALID;
}


/**
 * bzrpc_param_clear - clear RPC parameter
 * @ctx[in]: bzrpc context 
 *
 */
void bzrpc_param_clear(bzrpc_ctx_t *ctx)
{
	if(ctx != NULL && ctx->spec != NULL 
	  && ctx->spec->param_clear == NULL )
	{
		return ctx->spec->param_clear(ctx->obj);
	}
}

/**
 * bzrpc_param_get_size - Get num of bzrpc pramaters
 * @ctx[in]: bzrpc context 
 * Return: num of bzrpc parameters
 *
 */

int bzrpc_param_size_get(bzrpc_ctx_t *ctx)
{
	if(ctx != NULL && ctx->spec != NULL 
	  && ctx->spec->param_size_get == NULL )
	{
		return ctx->spec->param_size_get(ctx->obj);
	}
	return -1;
}



/**
 * bzrpc_result_add - add reuslt of rpc response
 * @ctx[in]: bzrpc context 
 * @result[in]: reulst of rpc result, it's a string generic.
 * Return: 0 when success, otherwise failed
 *
 */

int bzrpc_result_add(bzrpc_ctx_t *ctx, void *result)
{
	if(ctx == NULL || result == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->result_add != NULL)
		return ctx->spec->result_add(ctx->obj, result);
	
	return BZRPC_ERR_INVALID;
}

int bzrpc_result_add_str(bzrpc_ctx_t *ctx, const char *result)
{
	if(ctx == NULL || result == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->result_add_str != NULL)
		return ctx->spec->result_add_str(ctx->obj, result);
	
	return BZRPC_ERR_INVALID;
}




/**
 * bzrpc_result_get - get reuslt of rpc response
 * @ctx[in]: bzrpc context 
 * @result[out]: a point to reulst of rpc response.
 * Return: 0 when success, otherwise failed
 *
 */

int bzrpc_result_get(bzrpc_ctx_t *ctx, void **result)
{
	if(ctx == NULL || result == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->result_get != NULL)
		return ctx->spec->result_get(ctx->obj, result);
	
	return BZRPC_ERR_INVALID;
}


/**
 * bzrpc_result_size_get - get num of reuslt of rpc response
 * @ctx[in]: bzrpc context 
 * @result[out]: a point to reulst of rpc response.
 * Return: num of result if success else return -1;
 *
 */

int bzrpc_result_size_get(bzrpc_ctx_t *ctx)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->result_size_get != NULL)
		return ctx->spec->result_size_get(ctx->obj);
	
	return -1;
}


/**
 * bzrpc_result_clear - clear RPC result
 * @ctx[in]: bzrpc context 
 *
 */
void bzrpc_result_clear(bzrpc_ctx_t *ctx)
{
	if(ctx != NULL && ctx->spec != NULL 
	  && ctx->spec->result_clear == NULL )
	{
		ctx->spec->result_clear(ctx->obj);
	}
}




/**
 * bzrpc_error_code_get - get error code of rpc response
 * @ctx[in]: bzrpc context 
 * Return: error code of response
 *
 */

int bzrpc_error_code_get(bzrpc_ctx_t *ctx)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->error_code_get != NULL)
		return ctx->spec->error_code_get(ctx->obj);
	
	return BZRPC_INVALID_ID;
}


/**
 * bzrpc_error_code_set - set error code of rpc response
 * @ctx[in]: bzrpc context 
 * Return: error code of response
 *
 */

int bzrpc_error_code_set(bzrpc_ctx_t *ctx, int code)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL && ctx->spec->error_code_set != NULL)
		return ctx->spec->error_code_set(ctx->obj, code);
	
	return BZRPC_ERR_INVALID;
}


/**
 * bzrpc_error_msg_get - get error code of rpc response
 * @ctx[in]: bzrpc context 
 * @msg[out]: a point to bzrpc error msg, don't free by yourself
 * Return: error code of response
 *
 */

int bzrpc_error_msg_get(bzrpc_ctx_t *ctx, char **msg)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL &&  ctx->spec->error_msg_get != NULL)
		return ctx->spec->error_msg_get(ctx->obj, msg);
	
	return BZRPC_INVALID_ID;
}


/**
 * bzrpc_error_msg_set - set error msg of rpc response
 * @ctx[in]: bzrpc context 
 * Return: error code of response
 *
 */

int bzrpc_error_msg_set(bzrpc_ctx_t *ctx, const char *msg)
{
	if(ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(ctx->spec != NULL && ctx->spec->error_msg_set != NULL)
		return ctx->spec->error_msg_set(ctx->obj, msg);
	
	return BZRPC_ERR_INVALID;
}



int bzrpc_encode_request(bzrpc_ctx_t *ctx, uint8_t **data, int *len)
{
	int ret;
	if(ctx == NULL || data == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	if(ctx->spec != NULL && ctx->spec->encode_req != NULL)
	{
		return ctx->spec->encode_req(ctx->obj, data, len);
	}

	return BZRPC_ERR_INVALID;
}

 int bzrpc_decode_request(bzrpc_ctx_t *ctx, uint8_t *data, int len)
 {
	 
	 if(ctx == NULL || data == NULL)
	 {
		 BZRPC_LOG_ERR("Bad parameter!");
		 return BZRPC_ERR_BADPARAM;
	 }
 
	 if(ctx->spec != NULL && ctx->spec->decode_req != NULL)
	 {
		 return ctx->spec->decode_req(ctx->obj, data, len);
	 }
 
	 return BZRPC_ERR_INVALID;
 }


 int bzrpc_encode_response(bzrpc_ctx_t *ctx, uint8_t **data, int *len)
 {
	 int ret;
	 if(ctx == NULL || data == NULL)
	 {
		 BZRPC_LOG_ERR("Bad parameter!");
		 return BZRPC_ERR_BADPARAM;
	 }
 
	 if(ctx->spec != NULL && ctx->spec->encode_resp != NULL)
	 {
		 return ctx->spec->encode_resp(ctx->obj, data, len);
	 }
 
	 return BZRPC_ERR_INVALID;
 }
 
  int bzrpc_decode_response(bzrpc_ctx_t *ctx, uint8_t *data, int len)
  {
	  
	  if(ctx == NULL || data == NULL)
	  {
		  BZRPC_LOG_ERR("Bad parameter!");
		  return BZRPC_ERR_BADPARAM;
	  }
  
	  if(ctx->spec != NULL && ctx->spec->decode_resp != NULL)
	  {
		  return ctx->spec->decode_resp(ctx->obj, data, len);
	  }
  
	  return BZRPC_ERR_INVALID;
  }

  int bzrpc_recv_request(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn)
  {
  	uint8_t *data;
	int   len;
	int ret;
	
  	if(ctx == NULL || conn == NULL)
  	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
  	}

	ret = conn->recv(conn, &data, &len);
	if(ret)
	{
		if(ret != BZRPC_ERR_TIMEOUT)
			BZRPC_LOG_DEBUG("Faild to receive rpc message! ret = %d!", ret);
		return ret;
	}

	if(ctx->debug & BZRPC_DEBUG_REQ)
	{
		BZRPC_LOG_DEBUG("+++++++++++++BZRPC REQUEST++++++++++++++\n%s", data);
	}

	ret = bzrpc_decode_request(ctx, data, len);
	if(ret)
		BZRPC_LOG_ERR("Failed to decode rpc request message: msg=%s, ret=%d", data, ret);
	BZRPC_MEM_SAFE_FREE(&data);
	return ret;
  }

   int bzrpc_recv_response(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn)
  {
  	uint8_t *data;
	int   len;
	int ret;
	
  	if(ctx == NULL || conn == NULL)
  	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
  	}

	ret = conn->recv(conn, &data, &len);
	if(ret || len == 0)
	{
		if(ret != BZRPC_ERR_TIMEOUT)
			BZRPC_LOG_ERR("Faild to receive rpc message! ret = %d!", ret);
		return ret;
	}

	if(ctx->debug & BZRPC_DEBUG_RES)
	{
		BZRPC_LOG_DEBUG("+++++++++++++BZRPC RESPONSE++++++++++++++\n%s", data);
	}

	ret = bzrpc_decode_response(ctx, data, len);
	if(ret)
		BZRPC_LOG_ERR("Failed to decode rpc response message: msg=%s, ret=%d", data, ret);
	BZRPC_MEM_SAFE_FREE(&data);
	return ret;
  }

int bzrpc_reply(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn)
{
	uint8_t *data;
	int   len;
	int ret;
	
	if(ctx == NULL || conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	
	ret = bzrpc_encode_response(ctx, &data, &len);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to encode rpc request message! ret = %d!", ret);
		return ret;
	}

	if(ctx->debug & BZRPC_DEBUG_RES)
	{
		BZRPC_LOG_DEBUG("+++++++++++++BZRPC RESPONSE++++++++++++++");
		BZRPC_LOG_DEBUG("%s", data);
	}
	ret = conn->send(conn, data, len);
	BZRPC_MEM_SAFE_FREE(&data);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to write sock! ret = %d!", ret);
		return BZRPC_ERR_SOCK;
	}
	return 0;
}

 int bzrpc_request(bzrpc_ctx_t *ctx, struct bzrpc_conn *conn)
{
	uint8_t *data;
	int   len;
	int ret;
	
	if(ctx == NULL || conn == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	
	ret = bzrpc_encode_request(ctx, &data, &len);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to encode rpc request message, method=%s ret = %d!", BZRPC_METHOD_GET(ctx), ret);
		return ret;
	}

	if(ctx->debug & BZRPC_DEBUG_REQ)
	{
		BZRPC_LOG_DEBUG("+++++++++++++BZRPC REQUEST++++++++++++++\n%s", data);
	}
	
	ret = conn->send(conn, data, len);
	BZRPC_MEM_SAFE_FREE(&data);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to write sock! ret = %d!", ret);
		return BZRPC_ERR_SOCK;
	}
	if(bzrpc_id_get(ctx) == BZRPC_INVALID_ID)
		return 0;
	
	ret = bzrpc_recv_response(ctx, conn);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to receive rpc response message, method=%s, ret=%d", BZRPC_METHOD_GET(ctx), ret);
		return ret;
	}
	return 0;
}

 /**
  * bzrpc_addr_resolve - resolve RPC server address, RPC address format like following:
                      - <proto>://<host or path>[:port]
  * @addr[in]: address of rpc
  * @transport[out]: a point of transport name, it will alloc memory, you should free yourself.
  * @host[out]: a point of host of addresss, it will alloc memory, you should free yourself.
  * @port[out]: port of RPC address
  * Return: 0 if succcess, otherwise is error
  *
  */

int bzrpc_addr_resolve(const char *addr, char **transport, char **host, uint16_t *port)
{
	char *p = NULL, *h = NULL, *s;
	int len;
	int ret;

	if(addr == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	/*search proto*/
	s = strstr(addr, "://"); 
	if(s == NULL)
	{
		BZRPC_LOG_ERR("Bad RPC address: %s", addr);
		return BZRPC_ERR_BADPARAM;
	}
	len = s - addr;
	if(len <= 0)
	{
		BZRPC_LOG_ERR("Bad RPC address: %s", addr);
		return BZRPC_ERR_BADPARAM;
	}
	
	if(transport != NULL)
	{
		p = calloc(len, 1);
		if(p == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory");
			return BZRPC_ERR_MEM;
		}
		strncpy(p, addr, len);
		*transport = p;
	}
	addr = s+3; /*skip ://*/

	/*search host or path*/
	s = strchr(addr, ':'); 
	if(s == NULL) 
	{
		/*no port*/
		if(port == NULL) 
		{
			BZRPC_LOG_ERR("Bad RPC address: %s", addr);
			ret =  BZRPC_ERR_BADPARAM;
			goto err;
		}
		len = strlen(addr);
	}
	else
		len = s - addr;
	if(len <= 0)
	{
		BZRPC_LOG_ERR("Bad RPC address: %s", addr);
		ret =  BZRPC_ERR_BADPARAM;
		goto err;
	}
	if(host != NULL)
	{
		h = calloc(len, 1);
		if(h == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory");
			ret =  BZRPC_ERR_MEM;
			goto err;
		}
		strncpy(h, addr, len);
		*host = h;
	}

	/*no port*/
	if(s == NULL)
	{
		if(port != NULL)
			*port = 0;
		return 0;
	}
	
	addr = s+1;
	if(*addr == 0)
	{
		BZRPC_LOG_ERR("Bad RPC address: %s", addr);
		ret =  BZRPC_ERR_BADPARAM;
		goto err;
	}
	if(port != NULL)
		*port = atoi(addr);

	return 0;

err:
	if(p != NULL)
		free(p);
	if(h != NULL)
		free(h);
	return ret;
	
}


void bzrpc_set_debug_flag(bzrpc_ctx_t *ctx, int flag)
{
	ctx->debug = flag;
}


static inline uint32_t bzrpc_method_hash(const char *method)
{
	uint32_t hash = 0;
	char c;
	if(method == NULL)
		return 0;
	while( (c = *method++) != 0)
		hash += c;
	return hash;
}

static struct bzrpc_procedure *bzrpc_lookup_procedure(char *method, struct bzrpc_server *server)
{
	struct bzrpc_procedure *p;
	uint32_t index;

	index = bzrpc_method_hash(method) % BZRPC_METHOD_HASH_SIZE;

	TAILQ_FOREACH(p, &server->procedure_list[index], list)
	{
		if(strcmp(p->method, method) == 0)
			return p;
	}
	return NULL;
}


static int bzrpc_exec_request(struct bzrpc_server *server, bzrpc_ctx_t *rpc, struct bzrpc_conn *conn)
{
	char *method;
	int ret;
	struct bzrpc_procedure *p;

	method = BZRPC_METHOD_GET(rpc);
//	ret = bzrpc_method_get(rpc, &method);
	if(method == NULL)
	{
		BZRPC_LOG_WARN("Could not get method!");
		return BZRPC_RET_REJECT;
	}
	p = bzrpc_lookup_procedure(method, server);
	if(p == NULL)
	{
		BZRPC_LOG_WARN("Could not find procedure:%s!", method);
		return BZRPC_RET_REJECT;
	}
	ret = p->func(rpc);
	return ret;
}


int bzrpc_server_accept(struct bzrpc_server *server, int sockfd, int close)
{
	bzrpc_ctx_t *rpc = NULL;
	struct bzrpc_conn *conn = NULL;
	struct bzrpc_procedure *p;
	int ret;

	if(server == NULL || server->new_ctx == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_RET_REJECT;
	}
	
	ret = server->new_ctx(server, &rpc);
	if(ret)
		return BZRPC_RET_REJECT;

	rpc->debug = server->debug;
	ret = _bzrpc_new_conn(server->transport, sockfd, NULL, &conn);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not create bzrpc connection, ret=%d", ret);
		goto out;
	}
	bzrpc_conn_set_timeout(conn, server->timeout);
	ret = bzrpc_recv_request(rpc, conn);
	if(ret)
	{
		if(ret != BZRPC_ERR_TIMEOUT)
			BZRPC_LOG_ERR("Failed to receive RPC request, ret=%d", ret);
		ret = BZRPC_RET_REJECT;
		goto out;
	}

	ret = bzrpc_exec_request(server, rpc, conn);
	if(ret == BZRPC_RET_SUCESS)
	{
		ret = bzrpc_reply(rpc, conn);
		if(ret)
			BZRPC_LOG_WARN("Failed to reply RPC request!, ret=%d", ret);
	}
	else if(ret == BZRPC_RET_REJECT)
		BZRPC_LOG_WARN("RPC request reject");
	if(close)
		bzrpc_close_conn(conn);

out:
	if(conn != NULL)
		bzrpc_destroy_conn(conn);
	if(rpc != NULL)
		bzrpc_ctx_destroy(rpc);
	return ret;
}


struct bzrpc_procedure *bzrpc_procedure_new(const char *method, bzrpc_func_t *func, int multithread)
{
	struct bzrpc_procedure *p;
	
	p = bzrpc_mem_alloc(sizeof(struct bzrpc_procedure));
	if(p == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory for RPC procedure!");
		return NULL;
	}
	p->method = strdup(method);
	p->func = func;
	p->multithread = multithread;
	BZRPC_LOG_INFO("Registered procedure:%s", method);
	return p;
}

void bzrpc_procedure_destroy(struct bzrpc_procedure *p)
{
	if(p != NULL)
	{
		if(p->method != NULL)
			bzrpc_mem_free(p->method);
		bzrpc_mem_free(p);
	}
}


struct bzrpc_server *bzrpc_server_new(const char *name, const char *addr)
{
	struct bzrpc_transport *trans;
	struct bzrpc_server *server;
	char *transport;
	int ret, i;

	if(addr == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return NULL;
	}

	ret = bzrpc_addr_resolve(addr, &transport, NULL, NULL);
	if(ret)
	{
		BZRPC_LOG_ERR("Could resolve server address:%s", addr);
		return NULL;
	}

	trans = bzrpc_lookup_transport(transport);
	if(trans == NULL)
	{
		BZRPC_LOG_ERR("Could not found transport:%s", transport);
		free(transport);
		return NULL;
	}
	free(transport);

	if(trans->setup_server == NULL)
	{
		BZRPC_LOG_ERR("setup_server of transport was not defined");
		return NULL;
	}

	server = bzrpc_mem_alloc(sizeof(struct bzrpc_server));
	if(server == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory!");
		return NULL;
	}
	ret = trans->setup_server(server, addr);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not setup rpc server!");
		free(server);
		return NULL;
	}
	if(name != NULL)
		server->name = strdup(name);
	else
		server->name = strdup("bzrpc-server");

	server->addr = strdup(addr);
	server->transport = trans;
	
	for(i = 0; i < BZRPC_METHOD_HASH_SIZE; i++)
		TAILQ_INIT(&server->procedure_list[i]);

	BZRPC_LOG_INFO("%s created", server->name);
	
	return server;
}

void bzrpc_server_destroy(struct bzrpc_server *server)
{
	struct bzrpc_procedure *p, *n;
	int i;
	
	if(server == NULL)
	{
		BZRPC_LOG_WARN("Bad parameter!");
		return;
	}

	BZRPC_LOG_INFO("%s destroyed", server->name);
	
	if(server->transport != NULL && server->transport->clear_server != NULL)
		server->transport->clear_server(server);

	if(server->name != NULL)
		bzrpc_mem_free(server->name);
	if(server->addr != NULL)
		bzrpc_mem_free(server->addr);

	for(i = 0; i < BZRPC_METHOD_HASH_SIZE; i++)
	{
		p = TAILQ_FIRST(&server->procedure_list[i]);
		while(p != NULL)
		{
			n = TAILQ_NEXT(p, list);
			bzrpc_procedure_destroy(p);
			p = n;
		}
	}
	bzrpc_mem_free(server);
}


int bzrpc_server_start(struct bzrpc_server *server)
{
	if(server == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(server->start == NULL)
	{
		BZRPC_LOG_ERR("start of RPC server was not implemented!");
		return BZRPC_ERR_INVALID;
	}
	return server->start(server, bzrpc_server_accept);
}

int bzrpc_server_stop(struct bzrpc_server *server)
{
	if(server == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(server->stop == NULL)
	{
		BZRPC_LOG_ERR("stop of RPC server was not implemented!");
		return BZRPC_ERR_INVALID;
	}
	return server->stop(server);
}


int bzrpc_register_procedure(struct bzrpc_server *server, const char *method, bzrpc_func_t *func, int multithread)
{
	struct bzrpc_procedure *p;
	uint32_t index;
	
	if(server == NULL || method == NULL || func == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	p = bzrpc_procedure_new(method, func, multithread);
	if(p == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc procedure");
		return BZRPC_ERR_MEM;
	}

	index = bzrpc_method_hash(method) % BZRPC_METHOD_HASH_SIZE;

	TAILQ_INSERT_TAIL(&server->procedure_list[index], p, list);
	return 0;
	
}


int bzrpc_call(bzrpc_ctx_t *ctx, const char *addr)
{
	int ret;
	struct bzrpc_conn *conn;

	if(ctx == NULL || addr == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	conn = bzrpc_new_client(addr);
	if(conn == NULL)
	{
		BZRPC_LOG_ERR("Could not create RPC client");
		return BZRPC_ERR_FAIL;
	}

	ret = bzrpc_request(ctx, conn);
	if(ret)
		BZRPC_LOG_ERR("Failed to request RPC, ret=%d", ret);
		
	bzrpc_destroy_client(conn);
	return ret;
}



static int bzrpc_parse_params(const char *params, char **name, char **value, char **end)
{
	char *s, *p;
	char *n = NULL, *v = NULL;
	int len;
	int ret;

	if(params == NULL)
	{
		BZRPC_LOG_ERR("Bad Parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	s = strchr(params, '='); 
	if(s == NULL)
	{
		BZRPC_LOG_ERR("Bad Parameter: %s!", params);
		return BZRPC_ERR_FAIL;
	}
	len = s - params;
	if(len <= 0)
	{
		BZRPC_LOG_ERR("Bad Parameter!");
		return BZRPC_ERR_BADPARAM;
	}
	if(name != NULL)
	{
		n = calloc(len, 1);
		if(n == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory!");
			ret = BZRPC_ERR_MEM;
			goto err;
		}
		
		strncpy(n, params, len);
		*name = n;
	}
	
	params = s+1;

	s = strchr(params, ','); 
	
	if(s == NULL)
	{
		len = strlen(params);
		*end = (char *)(params + len);
	}
	else
	{
	/*	len = s - params;*/
		*end = s + 1;
	}
	if(len <= 0)
	{
		ret = BZRPC_ERR_BADPARAM;
		BZRPC_LOG_ERR("Bad Parameter!");
		goto err;
	}
	if(value != NULL)
	{
		v = calloc(len, 1);
		if(v == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory!");
			ret = BZRPC_ERR_MEM;
			goto err;
		}
		strncpy(v, params, len);
		*value = v;
	}
	return 0;
	
err:
	if(n != NULL)
		free(n);
	if(v != NULL)
		free(v);
	return ret;

	
}

int bzrpc_fcall(bzrpc_ctx_t **ctx, const  char *addr, const char *method, const char *params, int id)
{
	bzrpc_ctx_t *c;
	char *name, *value, *p;
	int ret;

	if(ctx == NULL || addr == NULL || method == NULL)
	{
		BZRPC_LOG_ERR("Bad parameter!");
		return BZRPC_ERR_BADPARAM;
	}

	c = bzrpc_ctx_new("jsonrpc2.0");
	if(c == NULL)
	{
		BZRPC_LOG_ERR("Could not create RPC ctx!");
		return BZRPC_ERR_FAIL;
	}

	ret = bzrpc_method_set(c, method);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not set RPC method: %s!, ret=%d", method, ret);
		goto out;
	}

	while(!(ret = bzrpc_parse_params(params, &name, &value, &p)))
	{
		ret = bzrpc_param_add_str(c, name, value);
		free(name);
		free(value);
		if(ret)
		{
			BZRPC_LOG_ERR("Could not add RPC parameter! ret=%d", ret);
			goto out;
		}
		if(*p != 0)
		{
			params = p;
			continue;
		}
		break;
	}

	if(ret)
		goto out;

	bzrpc_id_set(c, id);
	ret = bzrpc_call(c, addr);
	if(ret)
	{
		BZRPC_LOG_ERR("Failed to call method: %s, parameter:%s, addr:%s, ret=%d", method, params, addr, ret);
		goto out;
	}

	*ctx = c;

	return 0;

out:
	bzrpc_ctx_destroy(c);
	return ret;
	
}




