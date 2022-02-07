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
#include "cJSON.h"


#ifndef TAILQ_FOREACH_SAFE

#define	TAILQ_FOREACH_SAFE(var, head, field, next)			\
	for ((var) = ((head)->tqh_first);				\
	    (var) != TAILQ_END(head) &&					\
	    ((next) = TAILQ_NEXT(var, field), 1); (var) = (next))

#endif

struct jsonrpc_ctx{
	int                   id;
	cJSON                 *method;
	cJSON                *param;
	cJSON                *result;
	cJSON                *error;
};




#define __JSONRPC_VER     "2.0"
#define __JSONRPC_PARAMS   "params"
#define __JSONRPC_RESULT   "result"
#define __JSONRPC          "jsonrpc"
#define __JSONRPC_METHOD   "method"
#define __JSONRPC_ERROR    "error"
#define __JSONRPC_CODE     "code"
#define __JSONRPC_MESSAGE  "message"
#define __JSONRPC_ID       "id"




static int jsonrpc_ctx_new(void **ctx)
{
	struct jsonrpc_ctx *c;

	c = bzrpc_mem_alloc(sizeof(struct jsonrpc_ctx));
	if(c == NULL)
		return BZRPC_ERR_MEM;
	c->id = BZRPC_INVALID_ID;
	*ctx = c;
	return 0;
}

static void jsonrpc_request_destory(struct jsonrpc_ctx *ctx)
{
	if(ctx->method != NULL)
	{
		cJSON_Delete(ctx->method);
	}
	if(ctx->param != NULL)
	{
		cJSON_Delete(ctx->param);
		ctx->param = NULL;
	}
}

static void jsonrpc_response_destory(struct jsonrpc_ctx *ctx)
{
	if(ctx->result != NULL)
	{
		cJSON_Delete(ctx->result);
		ctx->result = NULL;
	}
	if(ctx->error != NULL)
	{
		cJSON_Delete(ctx->error);
		ctx->error = NULL;
	}
}


static void jsonrpc_ctx_destroy(void *ctx)
{
	if(ctx != NULL)
	{
		jsonrpc_request_destory((struct jsonrpc_ctx *)ctx);
		jsonrpc_response_destory((struct jsonrpc_ctx *)ctx);
		bzrpc_mem_free(ctx);
 	}
}


static int jsonrpc_id_get(void *context)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	return ctx->id;
}

static int jsonrpc_id_set(void *context, int id)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	ctx->id = id;
	return 0;
}

static int jsonrpc_method_get(void *context, char **method)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	if(ctx->method != NULL)
	{
		*method = ctx->method->valuestring;
		return 0;
	}
	return BZRPC_ERR_NOTFOUND;
}

static int jsonrpc_method_set(void *context, const char *method)
{
	char *s = NULL;
	cJSON *item = NULL;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;

	if(method != NULL)
	{
		item = cJSON_CreateString(method);
		if(item == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory!");
			return BZRPC_ERR_MEM;
		}
	}

	if(ctx->method != NULL)
	{
		cJSON_Delete(ctx->method);
		ctx->method = NULL;
	}

	ctx->method = item;
	return 0;
}


static int jsonrpc_param_add(void *context, const char *name, void *obj)
{
	cJSON *param;
	cJSON *item = (cJSON *)obj;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(item == NULL)
		return 0;
	
	if(ctx->param == NULL)
	{
		param = cJSON_CreateObject();
		if(param == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory!");
			return BZRPC_ERR_MEM;
		}
		ctx->param = param;
	}
	else
		param = ctx->param;
	cJSON_AddItemToObject(param, name, item);
	return 0;
}

static int jsonrpc_param_add_str(void *context, const char *name, const char *param)
{
	cJSON *item;
	int ret;
	int len;
	
	item = cJSON_CreateString(param);
	if(item == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory!");
		return BZRPC_ERR_MEM;
	}
	ret = jsonrpc_param_add(context, name, item);
	if(ret){
		BZRPC_LOG_ERR("Could add parameter!");
		cJSON_Delete(item);
		return ret;
	}
	return 0;
}


static int jsonrpc_param_get(void *context, const char *name, void **obj)
{
	cJSON *param;
	cJSON *item;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;

	param = ctx->param;
	if(param == NULL)
		return BZRPC_ERR_NOTFOUND;
	item = cJSON_GetObjectItem(param, name);
	if(item  == NULL)
		return BZRPC_ERR_NOTFOUND;
	*obj = item;
	return 0;
}


static void jsonrpc_param_clear(void *context)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	if(ctx->param != NULL)
	{
		cJSON_Delete(ctx->param);
		ctx->param = NULL;
	}
}

static int jsonrpc_param_size_get(void *context)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	if(ctx->param != NULL)
		return cJSON_GetArraySize(ctx->param);
	return 0;
}



static int jsonrpc_result_add(void *context, void *result)
{
	cJSON *root;
	cJSON *item = (cJSON *)result;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(item == NULL)
		return 0;
	if(ctx->result == NULL)
	{
		root = cJSON_CreateArray();
		if(root == NULL)
		{
			BZRPC_LOG_ERR("Could not alloc memory!");
			return BZRPC_ERR_MEM;
		}
		ctx->result = root;
	}
	else
		root = ctx->result;
	cJSON_AddItemToArray(root, item);
	return 0;
}

static int jsonrpc_result_add_str(void *context, const char *result)
{
	cJSON *item;
	int ret;
	int len;
	
	item = cJSON_CreateString(result);
	if(item == NULL)
	{
		BZRPC_LOG_ERR("Could not alloc memory!");
		return BZRPC_ERR_MEM;
	}
	ret = jsonrpc_result_add(context, item);
	if(ret){
		BZRPC_LOG_ERR("Could add result!");
		cJSON_Delete(item);
		return ret;
	}
	return 0;
}

static int jsonrpc_result_get_byindex(void *context, int index, void **result)
{
	cJSON *root, *item;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(ctx->result == NULL)
		return BZRPC_ERR_NOTFOUND;
	root = ctx->result;
	item = cJSON_GetArrayItem(root, index);
	if(item != NULL)
	{
		*result = item;
		return 0;
	}
	*result == NULL;
	return BZRPC_ERR_NOTFOUND;
}




static int jsonrpc_result_get(void *context, void **result)
{
	return jsonrpc_result_get_byindex((struct jsonrpc_ctx *)context, 0, result);
}



static int jsonrpc_result_size_get(void *context)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	if(ctx->result != NULL)
		return cJSON_GetArraySize(ctx->result);
	return 0;
}

static void jsonrpc_result_clear(void *context)
{
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	if(ctx->result != NULL)
	{
		cJSON_Delete(ctx->result);
		ctx->result = NULL;
	}
}

static int jsonrpc_error_code_set(void *context, int code)
{
	cJSON *c;
	cJSON *item;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(ctx->error == NULL)
	{
		c = cJSON_CreateObject();
		if(c == NULL)
			return BZRPC_ERR_MEM;
		ctx->error = c;
	}
	else
		c = ctx->error;
	cJSON_DeleteItemFromObject(c, __JSONRPC_CODE);
	item = cJSON_CreateNumber(code);
	if(item == NULL)
		return BZRPC_ERR_MEM;
	cJSON_AddItemToObject(c, __JSONRPC_CODE, item);
	
	return 0;;
}

static int jsonrpc_error_code_get(void *context)
{
	cJSON *c;
	cJSON *item;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(ctx->error == NULL)
		return BZRPC_INVALID_ID;

	item = cJSON_GetObjectItem(ctx->error, __JSONRPC_CODE);
	if(item != NULL && item->type == cJSON_Number)
		return item->valueint;

	return BZRPC_INVALID_ID;
}

static int jsonrpc_error_msg_set(void *context, const char *msg)
{
	cJSON *c;
	cJSON *item;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(ctx->error == NULL)
	{
		c = cJSON_CreateObject();
		if(c == NULL)
			return BZRPC_ERR_MEM;
		ctx->error = c;
	}
	else
		c = ctx->error;
	cJSON_DeleteItemFromObject(c, __JSONRPC_MESSAGE);
	if(msg != NULL)
	{
		item = cJSON_CreateString(msg);
		if(item == NULL)
			return BZRPC_ERR_MEM;
		cJSON_AddItemToObject(c, __JSONRPC_MESSAGE, item);
	}
	
	return 0;;
}

static int jsonrpc_error_msg_get(void *context, char **msg)
{
	cJSON *c;
	cJSON *item;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;
	
	if(ctx->error == NULL)
		return BZRPC_ERR_NOTFOUND;

	item = cJSON_GetObjectItem(ctx->error, __JSONRPC_MESSAGE);
	if(item != NULL && item->type == cJSON_String)
	{
		*msg = item->valuestring;
		return 0;
	}

	return BZRPC_ERR_NOTFOUND;
}


static int jsonrpc_decode_req(void *context, uint8_t *data, int len)
{
	cJSON *root;
	cJSON *item;
	int ret;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;

	root = cJSON_Parse(data);
	if(root == NULL)
		return BZRPC_ERR_DECODE;
	ctx->method = cJSON_DetachItemFromObject(root, __JSONRPC_METHOD);
	ctx->param = cJSON_DetachItemFromObject(root, __JSONRPC_PARAMS);;	
	item = cJSON_GetObjectItem(root, __JSONRPC_ID);
	if(item != NULL && item->type == cJSON_Number)
		ctx->id = item->valueint;
	else 
		ctx->id = BZRPC_INVALID_ID;

	cJSON_Delete(root);
	return 0;
	
}

static int jsonrpc_encode_req(void *context, uint8_t **data, int *len)
{
	cJSON *root;
	cJSON *param;
	int i;
	char *s;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;

	root = cJSON_CreateObject();
	if(root == NULL)
		return BZRPC_ERR_MEM;
	cJSON_AddStringToObject(root, __JSONRPC, __JSONRPC_VER);
	cJSON_AddItemToObject(root, __JSONRPC_METHOD, ctx->method);
	if(ctx->param != NULL)
		cJSON_AddItemToObject(root, __JSONRPC_PARAMS, ctx->param);
	cJSON_AddNumberToObject(root, __JSONRPC_ID, ctx->id);
	s = cJSON_PrintUnformatted(root);
	cJSON_DetachItemFromObject(root, __JSONRPC_PARAMS); /*deattach params to prevent cJSON_Delete params item*/
	cJSON_DetachItemFromObject(root, __JSONRPC_METHOD); /*deattach method to prevent cJSON_Delete method item*/
	cJSON_Delete(root);
	if(s == NULL)
		return BZRPC_ERR_ENCODE;
	*data = s;
	if(len != NULL)
		*len = strlen(s) + 1;
	return 0;
}


static int jsonrpc_encode_resp(void *context, uint8_t **data, int *len)
{
	cJSON *root;	
	char *s;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;

	root = cJSON_CreateObject();
	if(root == NULL)
		return BZRPC_ERR_MEM;
	cJSON_AddStringToObject(root, __JSONRPC, __JSONRPC_VER);
	cJSON_AddItemToObject(root, __JSONRPC_RESULT, ctx->result);
	cJSON_AddItemToObject(root, __JSONRPC_ERROR, ctx->error);
	cJSON_AddNumberToObject(root, __JSONRPC_ID, ctx->id);
	s = cJSON_PrintUnformatted(root);
	cJSON_DetachItemFromObject(root, __JSONRPC_RESULT); /*deattach result to prevent cJSON_Delete result item*/
	cJSON_DetachItemFromObject(root, __JSONRPC_ERROR); /*deattach result to prevent cJSON_Delete result item*/
	cJSON_Delete(root);
	if(s == NULL)
		return BZRPC_ERR_ENCODE;
	*data = s;
	if(len != NULL)
		*len = strlen(s) + 1;
	return 0;
}



static int jsonrpc_decode_resp(void *context, uint8_t *data, int len)
{
	cJSON *root;
	cJSON *item;
	int ret;
	struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *)context;

	root = cJSON_Parse(data);
	if(root == NULL)
		return BZRPC_ERR_DECODE;

	ctx->result = cJSON_DetachItemFromObject(root, __JSONRPC_RESULT);
	ctx->error = cJSON_DetachItemFromObject(root, __JSONRPC_ERROR);
	item = cJSON_GetObjectItem(root, __JSONRPC_ID);
	if(item != NULL && item->type == cJSON_Number)
		ctx->id = item->valueint;
	else 
		ctx->id = BZRPC_INVALID_ID;

	cJSON_Delete(root);
	return 0;

err:
	return ret;
	
}


static void jsonrpc_ctx_dump(void *context, bzrpc_printf_t *p, void *cookie)
{
	uint8_t *s;
	int ret;

	ret = jsonrpc_encode_req(context, &s, NULL);
	if(!ret)
	{
		if(s != NULL)
		{
			p(cookie, "Request: %s\n", s);
			free(s);
		}
	}
	ret = jsonrpc_encode_resp(context, &s, NULL);
	if(!ret)
	{
		if(s != NULL)
		{
			p(cookie, "Response: %s\n", s);
			free(s);
		}
	}
}


struct bzrpc_spec json_rpc_spec={
	.name = "jsonrpc2.0",
	.id = 0,
	.id_get = jsonrpc_id_get,
	.id_set = jsonrpc_id_set,
	.method_get = jsonrpc_method_get,
	.method_set = jsonrpc_method_set,
	.ctx_new = jsonrpc_ctx_new,
	.ctx_destroy = jsonrpc_ctx_destroy,
	.ctx_dump = jsonrpc_ctx_dump,
	.param_add = jsonrpc_param_add,
	.param_add_str = jsonrpc_param_add_str,
	.param_get = jsonrpc_param_get,
	.param_clear = jsonrpc_param_clear,
	.param_size_get = jsonrpc_param_size_get,
	.result_add = jsonrpc_result_add,
	.result_add_str = jsonrpc_result_add_str,
	.result_get = jsonrpc_result_get,
	.result_get_byindex = jsonrpc_result_get_byindex,
	.result_size_get = jsonrpc_result_size_get,
	.result_clear = jsonrpc_result_clear,
	.decode_req = jsonrpc_decode_req,
	.encode_req = jsonrpc_encode_req,
	.decode_resp = jsonrpc_decode_resp,
	.encode_resp = jsonrpc_encode_resp,
	.error_code_set = jsonrpc_error_code_set,
	.error_code_get = jsonrpc_error_code_get,
	.error_msg_set = jsonrpc_error_msg_set,
	.error_msg_get = jsonrpc_error_msg_get,
};




