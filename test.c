//#define _GNU_SOURCE
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

#include "cJSON.h"

#include "bzrpc.h"



int test_rpc_decode_encode(int argc, char *argv)
{
	uint8_t *s = NULL;
	int ret;
	
	bzrpc_ctx_t *ctx = bzrpc_ctx_new(NULL);
	if(ctx == NULL)
	{
		printf("Could not alloc bzrpc ctx!");
		return -1;
	}

	bzrpc_param_add_str(ctx, "portid", "1");
	bzrpc_param_add_str(ctx, "speed", "10000");
	bzrpc_method_set(ctx, "set_port_speed");
	bzrpc_id_set(ctx, 1);
	

	bzrpc_error_code_set(ctx, 1000);
	bzrpc_error_msg_set(ctx, "Could not alloc memory");
	bzrpc_result_add_str(ctx, "{speed:10,duplex:full,link:true}");
	bzrpc_result_add_str(ctx, "{speed:1000,duplex:half, link:true}");

	ret = bzrpc_encode_request(ctx, &s, NULL);
	if(!ret)
		printf("%s\n", s);
	else
		printf("Failed to encode request, ret=%d\n", ret);
	BZRPC_MEM_SAFE_FREE(&s);

	ret = bzrpc_encode_response(ctx, &s, NULL);
	if(!ret)
		printf("%s\n", s);
	else
		printf("Failed to encode response, ret=%d\n", ret);

	BZRPC_MEM_SAFE_FREE(&s);
	bzrpc_ctx_destroy(ctx);
	
	
	return ret;
	
}
int test_bzrpc_addr_decode(int argc, char *argv[])
{
	char *proto;
	char *host;
	uint16_t port;
	int ret;

	if(argc < 2)
	{
		printf("Bad parameter\n");
		return -1;
	}

	ret = bzrpc_addr_resolve(argv[1], &proto, &host, &port);
	if(ret)
	{
		printf("decode address error:%s\n", argv[1]);
		return -1;
	}
	printf("addr=%s, proto=%s, host=%s, port=%u\n", argv[1], proto, host, port);
	free(proto);
	free(host);
	return 0;
}


static int test_rpc_func(bzrpc_ctx_t *ctx)
{
	int ret;
	uint8_t *s;

#if 0
	ret = bzrpc_encode_request(ctx, &s, NULL);
	if(ret)
	{
		BZRPC_LOG_ERR("Decode request error");
		return BZRPC_RET_REJECT;
	}
	printf("request: %s\n", s);
	bzrpc_mem_free(s);
#endif

//	printf("%s:%s\n", __func__, BZRPC_METHOD_GET(ctx));
	
	bzrpc_result_add_str(ctx, "hello");
	bzrpc_error_code_set(ctx, 1000);
	bzrpc_error_msg_set(ctx, "world");
	
	
	return BZRPC_RET_SUCESS;
}


#define RPC_SPEC "jsonrpc2.0"

int test_bzrpc_server(int argc, char *argv[])
{
	struct bzrpc_server *server;
	int ret;

	if(argc < 2)
	{
		printf("Bad parameter\n");
		return -1;
	}
	

	server = bzrpc_server_new("rpc", argv[1]);
	if(server == NULL)
	{
		BZRPC_LOG_ERR("Could not create bzrpc server\n");
		return -1;
	}
	server->spec = RPC_SPEC;

	ret = bzrpc_register_procedure(server, "get", test_rpc_func, 0);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not register procedure");
		exit(-1);
	}

	ret = bzrpc_server_start(server);
	if(ret)
	{
		BZRPC_LOG_ERR("Could not start RPC server");
		exit(-1);
	}
	while(1)
		sleep(10);
	
}


int test_bzrpc_client(int argc, char *argv[])
{
	bzrpc_ctx_t *ctx;
	struct bzrpc_conn *conn;
	int count = 1;
	int ret;

	if(argc < 2)
	{
		printf("Bad parameter\n");
		exit(-1);
	}

	if(argc > 2)
	{
		count = atoi(argv[2]);
	}

	conn = bzrpc_new_client(argv[1]);
	if(conn == NULL)
	{
		BZRPC_LOG_ERR("Could not create rpc client");
		exit(-1);
	}
	while(count--)
	{
		ctx = bzrpc_ctx_new(RPC_SPEC);
		if(ctx == NULL)
		{
			BZRPC_LOG_ERR("Could not create RPC ctx");
			exit(-1);
		}
		
		bzrpc_method_set(ctx, "get");
		bzrpc_param_add_str(ctx, "portid", "1");
		bzrpc_param_add_str(ctx, "duplex", "full");
		bzrpc_id_set(ctx, 20);

		//bzrpc_set_debug_flag(ctx, 0xfff);
		ret = bzrpc_request(ctx, conn);
		if(ret)
		{
			BZRPC_LOG_ERR("Failed to request");
			exit(-1);
		}
		
	
#if 0
		if(bzrpc_call(ctx, argv[1]))
		{
			BZRPC_LOG_ERR("Failed to Call");
			exit(-1);
		}
#endif		
		bzrpc_ctx_destroy(ctx);
	}

	bzrpc_destroy_client(conn);
	return 0;
	
	
}



/*
	bzrpc get portid=1,speed=full udp://127.0.0.1:8081
*/

int test_main(int argc, char *argv[])
{

	bzrpc_ctx_t *ctx;
	char *method;
	char *params;
	char *addr;
	int id;
	int ret;
	uint8_t *s;
	
	const char *usage =  \
		"  Usage:\n" \
		"       bzrpc <method> <name=value,name=value...> <addr> [id]";

	if(argc < 4)
	{
		printf("%s\n", usage);
		exit(-1);
	}

	method = argv[1];
	params = argv[2];
	addr = argv[3];

	if(argc > 4)
		id = atoi(argv[4]);
	else
		id = BZRPC_INVALID_ID;

	ret = bzrpc_fcall(&ctx, addr, method, params,id);
	if(ret)
	{
		printf("Failed!\n");
		exit(-1);
	}

	bzrpc_ctx_dump(ctx, NULL, NULL);
	
	bzrpc_ctx_destroy(ctx);
	
	return 0;
}

