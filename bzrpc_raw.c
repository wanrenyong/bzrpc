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

#ifndef TAILQ_FOREACH_SAFE

#define	TAILQ_FOREACH_SAFE(var, head, field, next)			\
	for ((var) = ((head)->tqh_first);				\
	    (var) &&					\
	    ((next) = TAILQ_NEXT(var, field), 1); (var) = (next))

#endif


#define RAW_RPC_VERSION 1


#define ALLOC_MEM(sz)  calloc(1, sz)
#define FREE_MEM(d)    free(d)

struct raw_entry{
	char *name;
	struct bzrpc_data data;
	TAILQ_ENTRY(raw_entry) entry;
};

struct raw_content{
	int num_entries;
	TAILQ_HEAD(, raw_entry) entries;
};

struct raw_error{
	char *msg;
	int  code;
};

struct raw_ctx{
	int                   id;
	char                 *method;
	struct raw_content   *param;
	struct raw_content   *result;
	struct raw_error     *error;
	void                 *cookie;
};
typedef struct raw_ctx raw_ctx_t;

/* buf format:
*  REQUEST:  [head][method][param]
*  RESPONSE: [head][result][error]
*
*  head format:
*   [magic][id][version][check_sum][payload_size][method_size][param_size][result_siz][error_size]
*   magic:request is 0xdeadbeef response is 0xbeefdead
*
*  param and result fomat:
*  [uint32 name_Len][name][uint32 data_len][data]......[uint32 name_Len][name][uint32 data_len][data]
*
*  error format:
*  [uint32 msg_len][msg][int32 code]
*/

#define LEN_SIZE sizeof(uint32_t)
#define ERR_CODE_SIZE sizeof(int32_t)
#define REQUEST_MAGIC 0xdeadbeef
#define RESPONSE_MAGIC 0xbeefdead

struct raw_bufhead{
	uint16_t version;
	uint16_t checksum;        /*payload checksum*/
	uint32_t payload_size;
	uint32_t method_size;
	uint32_t param_size;
	uint32_t result_size;
	uint32_t error_size;
	int32_t id;
	uint32_t magic;  
	uint8_t payload[];
};

#define BUFHEAD_SIZE sizeof(struct raw_bufhead)  

struct raw_bufdata{
	uint32_t len;
	uint8_t data[];
};

typedef struct raw_bufhead raw_bufhead_t;

static int raw_ctx_new(void **ctx)
{
	raw_ctx_t *raw = ALLOC_MEM(sizeof(raw_ctx_t));
	if(raw == NULL){
		BZRPC_LOG_ERR("Could not alloc memory");
		return BZRPC_ERR_MEM;
	}
	*ctx = raw;
	return 0;
}

static void raw_error_destroy(struct raw_error *error)
{
	if(error == NULL)
		return;
	if(error->msg)
		FREE_MEM(error->msg);
	FREE_MEM(error);
}

static struct raw_error *_raw_error_new(char *msg, int code)
{
	struct raw_error *e = ALLOC_MEM(sizeof(struct raw_error));
	if(e == NULL){
		BZRPC_LOG_ERR("Could not alloc memory");
		return NULL;
	}
	e->code = code;
	e->msg = msg;
	return e;
	
}
static struct raw_error *raw_error_new(char *msg, int code)
{
	char *m = NULL;
	struct raw_error *e;
	if(msg != NULL){
		m = strdup(msg);
		if(m == NULL){
			BZRPC_LOG_ERR("Could not alloc memory");
			return NULL;
		}
	}
	e = _raw_error_new(m, code);
	if(e == NULL){
		BZRPC_LOG_ERR("Could not alloc erorr");
		if(m != NULL)
			FREE_MEM(m);
		return NULL;
	}
	return e;
}



static struct raw_entry *_raw_entry_new(char *name, void *data, int data_sz)
{
	struct raw_entry *e = ALLOC_MEM(sizeof(struct raw_entry));
	if(e == NULL){
		BZRPC_LOG_ERR("Could not alloc memory");
		return NULL;
	}
	e->name = name;
	e->data.addr = data;
	e->data.len = data_sz;
	return e;
}

static struct raw_entry *raw_entry_new(char *name, void *data, int data_sz)
{
	char *n = NULL;
	void *d = NULL;
	struct raw_entry *e;

	if(name != NULL){
		n = strdup(name);
		if(n == NULL){
			BZRPC_LOG_ERR("Could not alloc memory");
			return NULL;
		}
	}
	if(data != NULL){
		d = ALLOC_MEM(data_sz);
		if(d == NULL){
			BZRPC_LOG_ERR("Could not alloc memory");
			if(name != NULL)
				FREE_MEM(n);
			return NULL;
		}
		memcpy(d, data, data_sz);
	}

	e = _raw_entry_new(n, d, data_sz);
	if(e == NULL){
		if(name != NULL)
			FREE_MEM(n);
		if(data != NULL)
			FREE_MEM(d);
	}
	return e;
}

static void raw_entry_destroy(struct raw_entry *e)
{
	if(e != NULL)
	{
		if(e->name != NULL)
			FREE_MEM(e->name);
		if(e->data.addr != NULL)
			FREE_MEM(e->data.addr);
		FREE_MEM(e);
	}
}


static void raw_content_destroy(struct raw_content *c)
{
	struct raw_entry *e, *n;
	
	if(c == NULL)
		return;
	
	TAILQ_FOREACH_SAFE(e, &c->entries, entry, n){
		if(e->name != NULL)
			FREE_MEM(e->name);
		if(e->data.addr != NULL)
			FREE_MEM(e->data.addr);
		FREE_MEM(e);
	}
	FREE_MEM(c);
}

static struct raw_content *raw_content_new(void)
{
	struct raw_content *c = ALLOC_MEM(sizeof(struct raw_content));
	if(c == NULL){
		BZRPC_LOG_ERR("Could not alloc memory");
		return NULL;
	}
	TAILQ_INIT(&c->entries);
	return c;
}


static void raw_ctx_destroy(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	
	if(raw == NULL)
		return;
	
	if(raw->method != NULL)
		FREE_MEM(raw->method);

	raw_content_destroy(raw->param);
	raw_content_destroy(raw->result);
	raw_error_destroy(raw->error);
}






static int raw_id_get(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	return raw->id;
}

static int raw_id_set(void *ctx, int id)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	raw->id = id;
	return 0;
}

static int raw_method_set(void *ctx, const char *method)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;

	if(raw->method != NULL){
		FREE_MEM(raw->method);
		raw->method = NULL;
	}
	raw->method = strdup(method);
	if(raw->method == NULL){
		BZRPC_LOG_ERR("Could not alloc memory");
		return BZRPC_ERR_MEM;
	}
	return 0;
}

static int raw_method_get(void *ctx, char **method)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;

	*method = raw->method;
	return 0;
}

static void raw_ctx_dump(void *context, bzrpc_printf_t *p, void *cookie)
{
	p(cookie, "Not implement yet\n");
}

static struct raw_entry *raw_entry_lookup(struct raw_content *c, char *name)
{
	struct raw_entry *e;
	if(c == NULL)
		return NULL;
	
	TAILQ_FOREACH(e, &c->entries, entry)
	{
		if(strcmp(e->name, name) == 0)
			return e;
	}
	return NULL;
}

static struct raw_entry *raw_entry_getbyindex(struct raw_content *c, int index)
{
	struct raw_entry *e;
	int i = 0;

	if(c == NULL)
		return NULL;
	
	TAILQ_FOREACH(e, &c->entries, entry)
	{
		if(i++ == index)
			return e;
	}
	return NULL;
	
}


static void _raw_entry_add(struct raw_content *c, struct raw_entry *e)
{
	TAILQ_INSERT_TAIL(&c->entries, e, entry);
	c->num_entries++;
}

static int raw_entry_add(struct raw_content *c, char *name, void *data, int data_len)
{
	struct raw_entry *e;
	e = raw_entry_new(name, data, data_len);
	if(e == NULL){
		BZRPC_LOG_ERR("Could not alloc paramer entry");
		return BZRPC_ERR_MEM;
	}
	_raw_entry_add(c, e);
	return 0;
}

static int raw_param_add(void *ctx, const char *name, void *data)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	bzrpc_data_t *d = (bzrpc_data_t *)data;
	
	if(raw->param == NULL){
		raw->param = raw_content_new();
		if(raw->param == NULL){
			BZRPC_LOG_ERR("Could not alloc paramer");
			return BZRPC_ERR_MEM;
		}
	}
	return raw_entry_add(raw->param, (char *)name, d->addr, d->len);
}


static int raw_param_add_str(void *ctx, const char *name, const char *data)
{
	bzrpc_data_t d;
	d.addr = (void *)data;
	d.len = strlen(data)+1;
	return raw_param_add(ctx, name, &d);
}




static int raw_param_get(void *ctx, const char *name, void **data)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	
	struct raw_entry *e = raw_entry_lookup(raw->param, (char *)name);
	if(e == NULL){
		BZRPC_LOG_ERR("Could not find paramer:%s", name);
		return BZRPC_ERR_NOTFOUND;
	}
	if(data != NULL)
		*data = &e->data;
	return 0;
}

static void raw_param_clear(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->param != NULL){
		raw_content_destroy(raw->param);
		raw->param = NULL;
	}
}

static int raw_num_param_get(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->param != NULL){
		return raw->param->num_entries;
	}
	return 0;
}


static int raw_result_add(void *ctx, void *data)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	bzrpc_data_t *d = (bzrpc_data_t *)data;

	if(raw->result == NULL){
		raw->result = raw_content_new();
		if(raw->result == NULL){
			BZRPC_LOG_ERR("Could not alloc paramer");
			return BZRPC_ERR_MEM;
		}
	}
	return raw_entry_add(raw->result, NULL, d->addr, d->len);
}

static int raw_result_add_str(void *ctx, const char *data)
{
	bzrpc_data_t d;
	d.addr = (void *)data;
	d.len = strlen(data)+1;
	return raw_result_add(ctx, &d);
}




static int raw_result_get_byindex(void *ctx, int index, void **data )
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	
	struct raw_entry *e = raw_entry_getbyindex(raw->result, index);
	if(e == NULL)
		return BZRPC_ERR_NOTFOUND;
	if(data != NULL)
		*data = &e->data;	
	return 0;
}


static int raw_result_get(void *ctx, void **data)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	
	return  raw_result_get_byindex(ctx, 0, data);
}

static int raw_num_result_get(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->result != NULL){
		return raw->result->num_entries;
	}
	return 0;
}

static void raw_result_clear(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->result != NULL){
		raw_content_destroy(raw->result);
		raw->result = NULL;
	}
}

static int raw_error_code_get(void *ctx)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->error == NULL)
		return BZRPC_INVALID_ID;
	return raw->error->code;
}

static int raw_error_code_set(void *ctx, int code)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->error == NULL){
		raw->error = raw_error_new(NULL, code);
		if(raw->error == NULL){
			BZRPC_LOG_ERR("Could not alloc error");
			return BZRPC_ERR_MEM;
		}
	}
	else
		raw->error->code = code;
	return 0;
}

static int raw_error_msg_get(void *ctx, char **msg)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->error == NULL || raw->error->msg == NULL)
		*msg = NULL;
	*msg = raw->error->msg;
	return 0;
}

static int raw_error_msg_set(void *ctx, const char *msg)
{
	raw_ctx_t *raw = (raw_ctx_t *)ctx;
	if(raw->error == NULL){
		raw->error = raw_error_new((char *)msg, BZRPC_INVALID_ID);
		if(raw->error == NULL){
			BZRPC_LOG_ERR("Could not alloc error");
			return BZRPC_ERR_MEM;
		}
	}
	else{
		raw->error->msg = strdup(msg);
		if(raw->error->msg == NULL){
			BZRPC_LOG_ERR("Could not alloc memory");
			return BZRPC_ERR_MEM;
		}
	}
	return 0;
}

static inline int raw_string_size(const char *str)
{
	int len = LEN_SIZE;
	if(str != NULL)
		len += strlen(str) + 1;
	return len;
}

static inline int raw_data_size(void *data, int data_size)
{
	int len = LEN_SIZE;
	if(data != NULL)
		len += data_size;
	return len;
}

static int raw_entry_size(struct raw_entry *e)
{
	int len = 0;
	if(e != NULL){
		len += raw_string_size(e->name);
		len += raw_data_size(e->data.addr, e->data.len);
		return len;
	}
	return 0;
}

static int raw_error_size(struct raw_error *e)
{
	int len = 0;
	if(e != NULL)
	{
		len += raw_string_size(e->msg);
		len += ERR_CODE_SIZE;
	}
	return len;
}

static int raw_content_size(struct raw_content *c)
{
	struct raw_entry *e;
	int len = 0;
	
	if(c == NULL)
		return 0;

	TAILQ_FOREACH(e, &c->entries, entry)
	{
		len += raw_entry_size(e);
	}
	return len;
}

static int calc_request_size(raw_ctx_t *ctx)
{
	int len = 0;
	if(ctx == NULL)
		return 0;
	if(ctx->method != NULL)
		len += raw_string_size(ctx->method);
	len += raw_content_size(ctx->param);
	return len;
}

static int calc_respons_size(raw_ctx_t *ctx)
{
	int len = 0;
	if(ctx == NULL)
		return 0;
	len += raw_content_size(ctx->result);
	len += raw_error_size(ctx->error);
	return len;
}



static int raw_string_encode(uint8_t *buf, uint8_t **end, const char *str)
{
	int len;
	uint8_t *b;
	struct raw_bufdata *d;

	b = buf;
	d = (struct raw_bufdata *)b;
	if(str != NULL)
		len = strlen(str)+1;
	else
		len = 0;
	d->len = len;
	b = d->data;
	if(len > 0){
		strcpy(b, str);
		b += len;
	}
	if(end != NULL)
		*end = b;
	return b - buf;
}

static int raw_entry_encode(uint8_t *buf, uint8_t **end, struct raw_entry *e)
{
	struct raw_bufdata *d;
	uint8_t *b;
	int len;

	raw_string_encode(buf, &b, e->name);
	d = (struct raw_bufdata *)b;
	if(e->data.addr != NULL)
		len = e->data.len;
	else
		len = 0;
	d->len = len;
	b = d->data;
	if(len > 0){
		memcpy(b, e->data.addr, len);
		b += len;
	}
	if(end != NULL)
		*end = b;
	return b - buf;	
}

static int raw_content_encode(uint8_t *buf, uint8_t **end, struct raw_content *c)
{
	struct raw_entry *e;
	uint8_t *b;
	int len = 0;
		
	if(c == NULL)
	{
		if(end != NULL)
			*end = buf;
		return 0;
	}
	b = buf;
	TAILQ_FOREACH(e, &c->entries, entry)
	{
		raw_entry_encode(b, &b, e);
	}
	if(end != NULL)
		*end = b;
	return b - buf;
}

static int raw_error_encode(uint8_t *buf, uint8_t **end, struct raw_error *e)
{
	struct raw_bufdata *d;
	uint8_t *b;
	
	if(e != NULL)
	{
		raw_string_encode(buf, &b, e->msg);
		*((int32_t *)b) = e->code;
		b += ERR_CODE_SIZE;
		if(end != NULL)
			*end = b;
		return b - buf;
	}
	return 0;
}


static inline int raw_rpc_buf_alloc(int payload_len, uint8_t **buf, int *buf_len)
{
	uint8_t *b;
	raw_bufhead_t *buf_head;
	int len =  BUFHEAD_SIZE + payload_len;
	
	b = ALLOC_MEM(len);
	if(b == NULL){
		BZRPC_LOG_ERR("Could not alloc memory");
		return BZRPC_ERR_MEM;
	}
	buf_head = (raw_bufhead_t *)b;
	buf_head->version = RAW_RPC_VERSION;
	buf_head->payload_size = payload_len;
	
	*buf = b;
	*buf_len = len;
	
	return 0;
	
}

static int response_buf_alloc(raw_ctx_t *raw, uint8_t **buf, int *buf_len)
{
	int len;
	raw_bufhead_t *buf_head;
	uint8_t *b;

	len = calc_respons_size(raw);
	if(raw_rpc_buf_alloc(len, &b, buf_len)){
		BZRPC_LOG_ERR("Could not alloc raw rpc buf");
		return BZRPC_ERR_MEM;
	}
	*buf = b;
	buf_head = (raw_bufhead_t *)b;
	buf_head->magic = RESPONSE_MAGIC;
	return 0;
}

static int request_buf_alloc(raw_ctx_t *raw, uint8_t **buf, int *buf_len)
{
	int len;
	raw_bufhead_t *buf_head;
	uint8_t *b;

	len = calc_request_size(raw);
	if(raw_rpc_buf_alloc(len, &b, buf_len)){
		BZRPC_LOG_ERR("Could not alloc raw rpc buf");
		return BZRPC_ERR_MEM;
	}
	*buf = b;
	buf_head = (raw_bufhead_t *)b;
	buf_head->magic = REQUEST_MAGIC;
	return 0;
}



static int raw_request_encode(void *ctx, uint8_t **data, int *data_len)
{
	uint8_t *buf, *b;
	int len;
	raw_bufhead_t *buf_head;
	raw_ctx_t *raw = (raw_ctx_t *)ctx;

	if(raw->method == NULL){
		BZRPC_LOG_ERR("Not define method");
		return BZRPC_ERR_ENCODE;
	}
	
	if(request_buf_alloc(raw, &buf, &len)){
		BZRPC_LOG_ERR("Could not alloc RPC request buf");
		return BZRPC_ERR_MEM;
	}
	
	buf_head = (raw_bufhead_t *)buf;
	b = buf_head->payload;
	buf_head->method_size = raw_string_encode(b, &b, raw->method);
	buf_head->param_size = raw_content_encode(b, &b, raw->param);
	
	if(data != NULL)
		*data = buf;
	if(data_len != NULL)
		*data_len = len;
	return 0;
}


static int raw_data_decode(uint8_t **data, int *data_len, uint8_t *buf, int buf_len, uint8_t **end)
{
	uint8_t *d = NULL;
	struct raw_bufdata *bd;
	int len;
	
	if(buf_len < LEN_SIZE){
		BZRPC_LOG_ERR("Bad len, expected len=%ld, actual len=%d", 
			LEN_SIZE, buf_len);
		return BZRPC_ERR_DECODE;
	}
	bd = (struct raw_bufdata *)buf;
	len = bd->len;
	buf_len -= LEN_SIZE;
	buf = bd->data;
	if(buf_len < len){
		BZRPC_LOG_ERR("Bad len, expected len=%d, actual len=%d", 
			len, buf_len);
		return BZRPC_ERR_DECODE;
	}
	if(data != NULL){
		if(len > 0 ){
			d = ALLOC_MEM(len);
			if(d == NULL){
				BZRPC_LOG_ERR("Could not alloc memory");
				return BZRPC_ERR_MEM;
			}
			memcpy(d, buf, len);
		}
		*data = d;
	}
	if(data_len != NULL)
		*data_len = len;
	buf += len;
	if(end != NULL)
		*end = buf;
	return 0;
}

static int raw_entry_decode(struct raw_entry **entry, uint8_t *buf, int buf_len, uint8_t **end)
{
	uint8_t *name = NULL;
	uint8_t *data = NULL;
	uint8_t *b;
	struct raw_entry *e;
	int len;
	int data_len;
	int ret;
	
	b = buf;
	ret = raw_data_decode(&name, NULL, b, buf_len, &b);
	if(ret){
		BZRPC_LOG_ERR("Could not decode entry name");
		return ret;
	}
	len = b - buf;  /*decoded len*/
	buf_len -= len;
	ret = raw_data_decode(&data, &data_len, b, buf_len, &b);
	if(ret){
		BZRPC_LOG_ERR("Could not decode entry data");
		if(name != NULL)
			FREE_MEM(name);
		return ret;
	}
	e = _raw_entry_new(name, data, data_len);
	if(e == NULL){
		BZRPC_LOG_ERR("Could not decode entry data");
		if(name != NULL)
			FREE_MEM(name);
		if(data != NULL)
			FREE_MEM(data);
		return BZRPC_ERR_MEM;
	}
	*entry = e;
	if(end != NULL)
		*end = b;
	return 0;
}

static int raw_error_decode(struct raw_error **error, uint8_t *buf, int buf_len, uint8_t **end)
{
	uint8_t *msg = NULL;
	int code;
	uint8_t *b;
	struct raw_error *e;
	int len;
	int data_len;
	int ret;
	
	b = buf;
	ret = raw_data_decode(&msg, NULL, b, buf_len, &b);
	if(ret){
		BZRPC_LOG_ERR("Could not decode error msg");
		return ret;
	}
	len = b - buf;  /*decoded len*/
	buf_len -= len;
	if(buf_len < ERR_CODE_SIZE){
		BZRPC_LOG_ERR("Could not decode error msg");
		if(msg != NULL)
			FREE_MEM(msg);
		return BZRPC_ERR_DECODE;
	}
	code = *((int32_t *)b);
	b += ERR_CODE_SIZE;

	e = _raw_error_new(msg, code);
	if(e == NULL){
		BZRPC_LOG_ERR("Could not alloc erorr");
		if(msg != NULL)
			FREE_MEM(msg);
		return BZRPC_ERR_MEM;
	}
	if(end != NULL)
		*end = b;
	return 0;
}


static int raw_content_decode(struct raw_content **data, uint8_t *buf, int buf_len, uint8_t **end)
{
	struct raw_content *c;
	struct raw_entry *e;
	uint8_t *b; 
	int len;
	int ret;
	
	c = raw_content_new();
	if(c == NULL){
		BZRPC_LOG_ERR("Could not alloc raw content");
		return BZRPC_ERR_MEM;
	}
	b = buf;
	while(buf_len > 0){
		ret = raw_entry_decode(&e, b, buf_len, &b);
		if(ret){
			BZRPC_LOG_ERR("Could not decode raw entry");
			raw_content_destroy(c);
			return ret;
		}
		_raw_entry_add(c, e);
		len = b - buf; /*decoded len*/
		buf_len -= len;
		buf = b;
	}
	*data = c;	
	return 0;
}

static int raw_request_decode(void *context, uint8_t *data, int len)
{
	raw_bufhead_t *buf_head;
	uint8_t *buf;
	int buf_len;
	int decoded_len; /*len that has been decoded */
	int ret;
	raw_ctx_t *raw = (raw_ctx_t *)context;

	buf_head = (raw_bufhead_t *)data;
	if(len < BUFHEAD_SIZE)
	{
		BZRPC_LOG_ERR("RPC data len less than header, len=%d, header_len=%ld", \
			len, BUFHEAD_SIZE);
		return BZRPC_ERR_DECODE;
	}

	if(buf_head->magic != REQUEST_MAGIC || \
		buf_head->method_size <= LEN_SIZE){
		BZRPC_LOG_ERR("RPC buf is not request, magic=%x, method_size=%d", \
			buf_head->magic, buf_head->method_size);
		return BZRPC_ERR_DECODE;		
	}
	if(buf_head->payload_size < LEN_SIZE){
		BZRPC_LOG_ERR("Bad payload, size=%d, expected size=%ld", \
			buf_head->payload_size, LEN_SIZE);
		return BZRPC_ERR_DECODE;
	}
	buf = buf_head->payload;
	buf_len = buf_head->payload_size;
	/*decode method*/
	if(buf_len < buf_head->method_size){
		BZRPC_LOG_ERR("Bad method, size=%d, expected size=%d", \
			buf_len, buf_head->method_size);
		return BZRPC_ERR_DECODE;
	}
	ret = raw_data_decode((uint8_t **)(&raw->method), NULL, buf, buf_head->method_size, &buf);
	if(ret){
		BZRPC_LOG_ERR("Could not decode method");
		return ret;
	}

	/*decode parameter*/
	buf_len -= buf_head->method_size;
	if(buf_head->param_size > 0){
		if(buf_len < buf_head->param_size){
			BZRPC_LOG_ERR("Bad paramter size, expected size =%d, actual is %d", 
				buf_head->param_size, buf_len);
			return BZRPC_ERR_DECODE;
		}
		ret = raw_content_decode(&raw->param, buf, buf_head->param_size, &buf);
		if(ret){
			BZRPC_LOG_ERR("Could not decode parameter");
			return ret;
		}
	}
	raw->id = buf_head->id;
	return 0;
}


static int raw_response_decode(void *context, uint8_t *data, int len)
{
	raw_bufhead_t *buf_head;
	uint8_t *buf;
	int buf_len;
	int decoded_len; /*len that has been decoded */
	int ret;
	raw_ctx_t *raw = (raw_ctx_t *)context;

	buf_head = (raw_bufhead_t *)data;
	if(len < BUFHEAD_SIZE)
	{
		BZRPC_LOG_ERR("RPC data len less than header, len=%d, header_len=%ld", \
			len, BUFHEAD_SIZE);
		return BZRPC_ERR_DECODE;
	}

	if(buf_head->magic != RESPONSE_MAGIC ){
		BZRPC_LOG_ERR("RPC buf is not response, magic=%x", \
			buf_head->magic);
		return BZRPC_ERR_DECODE;		
	}
	if(buf_head->payload_size < LEN_SIZE){
		BZRPC_LOG_ERR("Bad payload, size=%d, expected size=%ld", \
			buf_head->payload_size, LEN_SIZE);
		return BZRPC_ERR_DECODE;
	}
	buf = buf_head->payload;
	buf_len = buf_head->payload_size;

	/*decode result*/
	if(buf_head->result_size > 0){
		if(buf_len < buf_head->result_size){
			BZRPC_LOG_ERR("Bad result, size=%d, expected size=%d", \
				buf_len, buf_head->result_size);
			return BZRPC_ERR_DECODE;
		}
		ret = raw_content_decode(&raw->result, buf, buf_head->result_size, &buf);
		if(ret){
			BZRPC_LOG_ERR("Could not decode parameter");
			return ret;
		}
	}

	/*decode error*/
	buf_len -= buf_head->result_size;
	if(buf_head->error_size > 0){
		if(buf_len < buf_head->error_size){
			BZRPC_LOG_ERR("Bad error, size=%d, expected size=%d", \
				buf_len, buf_head->error_size);
			return BZRPC_ERR_DECODE;
		}
		ret = raw_error_decode(&raw->error, buf, buf_head->error_size, &buf);
		if(ret){
			BZRPC_LOG_ERR("Could not decode parameter");
			return ret;
		}
	}
	raw->id = buf_head->id;
	return 0;
}


static int raw_response_encode(void *ctx, uint8_t **data, int *data_len)
{
	uint8_t *buf, *b;
	int len;
	raw_bufhead_t *buf_head;
	raw_ctx_t *raw = (raw_ctx_t *)ctx;

	if(response_buf_alloc(raw, &buf, &len)){
		BZRPC_LOG_ERR("Could not alloc RPC response buf");
		return BZRPC_ERR_MEM;
	}
	
	buf_head = (raw_bufhead_t *)buf;
	b = (uint8_t *)(buf_head->payload);
	buf_head->result_size = raw_content_encode(b, &b, raw->result);
	buf_head->error_size = raw_error_encode(b, &b, raw->error);
	
	if(data != NULL)
		*data = buf;
	if(data_len)
		*data_len = len;
	return 0;
}






#if 1

struct bzrpc_spec raw_rpc_spec={
	.name = "raw",
	.id = 0,
	.id_get = raw_id_get,
	.id_set = raw_id_set,
	.method_get = raw_method_get,
	.method_set = raw_method_set,
	.ctx_new = raw_ctx_new,
	.ctx_destroy = raw_ctx_destroy,
	.ctx_dump = raw_ctx_dump,
	.param_add = raw_param_add,
	.param_add_str = raw_param_add_str,
	.param_get = raw_param_get,
	.param_clear = raw_param_clear,
	.param_size_get = raw_num_param_get,
	.result_add = raw_result_add,
	.result_add_str = raw_result_add_str,
	.result_get = raw_result_get,
	.result_get_byindex = raw_result_get_byindex,
	.result_size_get = raw_num_result_get,
	.result_clear = raw_result_clear,
	.decode_req = raw_request_decode,
	.encode_req = raw_request_encode,
	.decode_resp = raw_response_decode,
	.encode_resp = raw_response_encode,
	.error_code_set = raw_error_code_set,
	.error_code_get = raw_error_code_get,
	.error_msg_set = raw_error_msg_set,
	.error_msg_get = raw_error_msg_get,
};
#endif

