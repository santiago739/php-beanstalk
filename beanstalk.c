/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2008 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Alexey Romanenko <santiago739@gmail.com>                     |
  +----------------------------------------------------------------------+
*/

/* $Id */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_beanstalk.h"
#include <zend_exceptions.h>

/* If you declare any globals in php_beanstalk.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(beanstalk)
*/

static int le_beanstalk_client;
static zend_class_entry *beanstalk_ce;
static zend_class_entry *beanstalk_exception_ce;
static zend_class_entry *spl_ce_RuntimeException = NULL;

const zend_function_entry beanstalk_functions[] = {
	PHP_ME(Beanstalk, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Beanstalk, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Beanstalk, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Beanstalk, put, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Beanstalk, useTube, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}	
};

static int beanstalk_client_disconnect(beanstalk_client *bs_client TSRMLS_DC);

/* {{{ beanstalk_module_entry
 */
zend_module_entry beanstalk_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"beanstalk",
	beanstalk_functions,
	PHP_MINIT(beanstalk),
	PHP_MSHUTDOWN(beanstalk),
	PHP_RINIT(beanstalk),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(beanstalk),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(beanstalk),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_BEANSTALK_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_BEANSTALK
ZEND_GET_MODULE(beanstalk)
#endif

static zend_class_entry *_beanstalk_get_exception_base(int root TSRMLS_DC)
{
#if HAVE_SPL
	if (!root) {
		if (!spl_ce_RuntimeException) {
			zend_class_entry **pce;
        
			if (zend_hash_find(CG(class_table), "runtimeexception",
				sizeof("RuntimeException"), (void **) &pce) == SUCCESS) {
				spl_ce_RuntimeException = *pce;
				return *pce;
			}
		} else {
			return spl_ce_RuntimeException;
		}
	}
#endif
#if (PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 2)
	return zend_exception_get_default();
#else
	return zend_exception_get_default(TSRMLS_C);
#endif
}

static void _beanstalk_client_free(beanstalk_client *bs_client) /* {{{ */
{
	efree(bs_client->host);
	efree(bs_client);
}
/* }}} */

static void _beanstalk_client_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) /* {{{ */
{
	beanstalk_client *bs_client = (beanstalk_client *) rsrc->ptr;
	beanstalk_client_disconnect(bs_client TSRMLS_CC);
	_beanstalk_client_free(bs_client);
}
/* }}} */

static int _beanstalk_client_get(zval *id, beanstalk_client **bs_client TSRMLS_DC)
{
    zval **conn;
    int resource_type;

    if (Z_TYPE_P(id) != IS_OBJECT || zend_hash_find(Z_OBJPROP_P(id), "connection",
                                  sizeof("connection"), (void **) &conn) == FAILURE) {
        return -1;
    }

    *bs_client = (beanstalk_client *) zend_list_find(Z_LVAL_PP(conn), &resource_type);

    if (!*bs_client || resource_type != le_beanstalk_client) {
            return -1;
    }

    return Z_LVAL_PP(conn);
}

static beanstalk_client* beanstalk_client_create(char *host, int host_len, unsigned short port, long timeout TSRMLS_DC) /* {{{ */
{
	beanstalk_client *bs_client;

	bs_client		 = emalloc(sizeof(beanstalk_client));
	bs_client->host   = emalloc(host_len + 1);
	bs_client->stream = NULL;
	bs_client->status = BEANSTALK_CLIENT_STATUS_DISCONNECTED;

	memcpy(bs_client->host, host, host_len);
	bs_client->host[host_len] = '\0';

	bs_client->port	= port;
	bs_client->timeout = timeout;

	return bs_client;
}
/* }}} */

static int beanstalk_client_disconnect(beanstalk_client *bs_client TSRMLS_DC) /* {{{ */
{
	int res = 0;

	if (bs_client->stream != NULL) {
		//redis_sock_write(redis_sock, "QUIT", sizeof("QUIT") - 1);

		bs_client->status = BEANSTALK_CLIENT_STATUS_DISCONNECTED;
		php_stream_close(bs_client->stream);
		bs_client->stream = NULL;

		res = 1;
	}

	return res;
}
/* }}} */

static int beanstalk_client_connect(beanstalk_client *bs_client TSRMLS_DC) /* {{{ */
{
	struct timeval tv, *tv_ptr = NULL;
	char *host = NULL, *hash_key = NULL, *errstr = NULL;
	int host_len, err = 0;

	if (bs_client->stream != NULL) {
		beanstalk_client_disconnect(bs_client TSRMLS_CC);
	}

	tv.tv_sec  = bs_client->timeout;
	tv.tv_usec = 0;

	host_len = spprintf(&host, 0, "%s:%d", bs_client->host, bs_client->port);

	if(tv.tv_sec != 0) {
		tv_ptr = &tv;
	}
	
	bs_client->stream = php_stream_xport_create(
		host, host_len, ENFORCE_SAFE_MODE, STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT,
		hash_key, tv_ptr, NULL, &errstr, &err
	);

	efree(host);

	if (!bs_client->stream) {
		efree(errstr);
		return -1;
	}

	php_stream_auto_cleanup(bs_client->stream);

	if(tv.tv_sec != 0) {
		php_stream_set_option(bs_client->stream, PHP_STREAM_OPTION_READ_TIMEOUT, 0, &tv);
	}
	php_stream_set_option(bs_client->stream, PHP_STREAM_OPTION_WRITE_BUFFER,
						  PHP_STREAM_BUFFER_NONE, NULL);

	bs_client->status = BEANSTALK_CLIENT_STATUS_CONNECTED;

	return 0;
}
/* }}} */



static int beanstalk_client_open(beanstalk_client *bs_client, int force_connect TSRMLS_DC) /* {{{ */
{
	int res = -1;

	switch (bs_client->status) {
		case BEANSTALK_CLIENT_STATUS_DISCONNECTED:
			return beanstalk_client_connect(bs_client TSRMLS_CC);

		case BEANSTALK_CLIENT_STATUS_CONNECTED:
			res = 0;
			break;

		case BEANSTALK_CLIENT_STATUS_UNKNOWN:
			if (force_connect > 0 && beanstalk_client_connect(bs_client TSRMLS_CC) < 0) {
				res = -1;
			} else {
				bs_client->status = BEANSTALK_CLIENT_STATUS_CONNECTED;
				res = 0;
			}
			break;
	}

	return res;
}
/* }}} */

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("beanstalk.global_value",	  "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_beanstalk_globals, beanstalk_globals)
	STD_PHP_INI_ENTRY("beanstalk.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_beanstalk_globals, beanstalk_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_beanstalk_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_beanstalk_init_globals(zend_beanstalk_globals *beanstalk_globals)
{
	beanstalk_globals->global_value = 0;
	beanstalk_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(beanstalk)
{
	zend_class_entry ce;
	INIT_CLASS_ENTRY(ce, "Beanstalk", beanstalk_functions);
	beanstalk_ce = zend_register_internal_class(&ce TSRMLS_CC);
	
	zend_class_entry exception_ce;
    INIT_CLASS_ENTRY(exception_ce, "BeanstalkException", NULL);
    beanstalk_exception_ce = zend_register_internal_class_ex(
        &exception_ce,
        _beanstalk_get_exception_base(0 TSRMLS_CC),
        NULL TSRMLS_CC
    );
	
	le_beanstalk_client = zend_register_list_destructors_ex(
		_beanstalk_client_dtor, NULL, "Beanstalk client", module_number
	);
	
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(beanstalk)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(beanstalk)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(beanstalk)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(beanstalk)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "beanstalk support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/* {{{ proto void Beanstalk::__construct() */
PHP_METHOD(Beanstalk, __construct)
{
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
		RETURN_FALSE;
	}
}
/* }}} */

/* {{{ proto boolean Beanstalk::connect(string host, int port [, int timeout])
 */
PHP_METHOD(Beanstalk, connect)
{
	zval *object;
	int host_len, id;
	char *host = NULL;
	long port;

	struct timeval timeout = {0L, 0L};
	beanstalk_client *bs_client = NULL;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Osl|l",
									 &object, beanstalk_ce, &host, &host_len, &port,
									 &timeout.tv_sec) == FAILURE) {
	   RETURN_FALSE;
	}

	if (timeout.tv_sec < 0L || timeout.tv_sec > INT_MAX) {
		zend_throw_exception(beanstalk_exception_ce, "Invalid timeout", 0 TSRMLS_CC);
		RETURN_FALSE;
	}

	bs_client = beanstalk_client_create(host, host_len, port, timeout.tv_sec TSRMLS_CC);

	if (beanstalk_client_open(bs_client, 1 TSRMLS_CC) < 0) {
		_beanstalk_client_free(bs_client);
		zend_throw_exception_ex(
			beanstalk_exception_ce, 0 TSRMLS_CC, "Can't connect to %s:%d", host, port
		);
		RETURN_FALSE;
	}
	
	id = zend_list_insert(bs_client, le_beanstalk_client);
	add_property_resource(object, "connection", id);
	
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto boolean Beanstalk::close()
 */
PHP_METHOD(Beanstalk, close)
{
    zval *object;
    beanstalk_client *bs_client = NULL;

    if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O",
        &object, beanstalk_ce) == FAILURE) {
        RETURN_FALSE;
    }

    if (_beanstalk_client_get(object, &bs_client TSRMLS_CC) < 0) {
        RETURN_FALSE;
    }

    if (beanstalk_client_disconnect(bs_client TSRMLS_CC)) {
        RETURN_TRUE;
    }

    RETURN_FALSE;
}



static void beanstalk_check_eof(beanstalk_client *bs_client TSRMLS_DC) {
    int eof = php_stream_eof(bs_client->stream);
    while(eof) {
        bs_client->stream = NULL;
        beanstalk_client_connect(bs_client TSRMLS_CC);
        eof = php_stream_eof(bs_client->stream);
    }
}

static int beanstalk_client_write(beanstalk_client *bs_client, char *cmd, size_t sz TSRMLS_DC)
{
    beanstalk_check_eof(bs_client TSRMLS_CC);
    return php_stream_write(bs_client->stream, cmd, sz);

    return 0;
}

static char *beanstalk_client_read(beanstalk_client *bs_client, int *buf_len TSRMLS_DC)
{

    char inbuf[1024];
    char *resp = NULL;

    beanstalk_check_eof(bs_client TSRMLS_CC);
    php_stream_gets(bs_client->stream, inbuf, 1024);
	
	*buf_len = strlen(inbuf) - 2;
	if(*buf_len >= 2) {
		resp = emalloc(1+*buf_len);
		memcpy(resp, inbuf, *buf_len);
		resp[*buf_len] = 0;
		return resp;
	} else {
		printf("protocol error \n");
		return NULL;
	}
	
/*
    switch(inbuf[0]) {

        case '-':
            return NULL;

        case '+':
        case ':':    
            // Single Line Reply 
            / :123\r\n /
            *buf_len = strlen(inbuf) - 2;
            if(*buf_len >= 2) {
                resp = emalloc(1+*buf_len);
                memcpy(resp, inbuf, *buf_len);
                resp[*buf_len] = 0;
                return resp;
            } else {
                printf("protocol error \n");
                return NULL;
            }

        case '$':
            *buf_len = atoi(inbuf + 1);
            resp = redis_sock_read_bulk_reply(redis_sock, *buf_len);
            return resp;

        default:
            printf("protocol error, got '%c' as reply type byte\n", inbuf[0]);
    }

    return NULL;
*/
}

static int beanstalk_str_left(char *haystack, int haystack_len, char *needle, int needle_len) /* {{{ */
{
	char *found;

	found = php_memnstr(haystack, needle, needle_len, haystack + haystack_len);
	if ((found - haystack) == 0) {
		return 1;
	}
	return 0;
}
/* }}} */

static int beanstalk_parse_response_error(char *response, int response_len TSRMLS_DC) /* {{{ */
{
    if(beanstalk_str_left(response, response_len, "OUT_OF_MEMORY", sizeof("OUT_OF_MEMORY") - 1)) {
		return 0;
	}
	if(beanstalk_str_left(response, response_len, "INTERNAL_ERROR", sizeof("INTERNAL_ERROR") - 1)) {
		return 0;
	}
	if(beanstalk_str_left(response, response_len, "DRAINING", sizeof("DRAINING") - 1)) {
		return 0;
	}
	if(beanstalk_str_left(response, response_len, "BAD_FORMAT", sizeof("BAD_FORMAT") - 1)) {
		return 0;
	}
	if(beanstalk_str_left(response, response_len, "UNKNOWN_COMMAND", sizeof("UNKNOWN_COMMAND") - 1)) {
		return 0;
	}
	return 1;
}

static int beanstalk_parse_response_put(char *response, int response_len TSRMLS_DC) /* {{{ */
{
    if(beanstalk_str_left(response, response_len, "INSERTED", sizeof("INSERTED") - 1)) {
		return 1;
	}
	if(beanstalk_str_left(response, response_len, "BURIED", sizeof("BURIED") - 1)) {
		return 1;
	}
	if(beanstalk_str_left(response, response_len, "EXPECTED_CRLF", sizeof("EXPECTED_CRLF") - 1)) {
		return 0;
	}
	if(beanstalk_str_left(response, response_len, "JOB_TOO_BIG", sizeof("JOB_TOO_BIG") - 1)) {
		return 0;
	}
	return 0;
}

/* {{{ proto boolean Beanstalk::put(string key, string value)
 */
PHP_METHOD(Beanstalk, put)
{
    zval *object;
    beanstalk_client *bs_client = NULL;
    char *value = NULL, *command;
    int value_len, command_len;
    long priority, delay, ttr;
    char *response;
    int response_len;

    if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ollls",
                                     &object, beanstalk_ce, &priority, &delay, &ttr,
                                     &value, &value_len) == FAILURE) {
        RETURN_FALSE;
    }

    if (_beanstalk_client_get(object, &bs_client TSRMLS_CC) < 0) {
        RETURN_FALSE;
    }

	command_len = spprintf(&command, 0, "put %d %d %d %d\r\n%s\r\n", 
							priority, delay, ttr, value_len, value);

    if (beanstalk_client_write(bs_client, command, command_len TSRMLS_CC) < 0) {
        efree(command);
        RETURN_FALSE;
    }
    efree(command);
    //beanstalk_boolean_response(INTERNAL_FUNCTION_PARAM_PASSTHRU, bs_client);
    
    if ((response = beanstalk_client_read(bs_client, &response_len TSRMLS_CC)) == NULL) {
        RETURN_FALSE;
    }
    printf("response: %s \n", response);
    
    if(beanstalk_parse_response_put(response, response_len TSRMLS_CC)) {
		efree(response);
		RETURN_TRUE;
	}
    
    efree(response);
    RETURN_FALSE;
    
}

/* {{{ proto boolean Beanstalk::useTube(string tube)
 */
PHP_METHOD(Beanstalk, useTube)
{
    zval *object;
    beanstalk_client *bs_client = NULL;
    char *tube = NULL, *command;
    int tube_len, command_len;
    char *response;
    int response_len;

    if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os",
                                     &object, beanstalk_ce, &tube, &tube_len) == FAILURE) {
        RETURN_FALSE;
    }

    if (_beanstalk_client_get(object, &bs_client TSRMLS_CC) < 0) {
        RETURN_FALSE;
    }

	command_len = spprintf(&command, 0, "use %s\r\n", tube);

    if (beanstalk_client_write(bs_client, command, command_len TSRMLS_CC) < 0) {
        efree(command);
        RETURN_FALSE;
    }
    efree(command);
    
    if ((response = beanstalk_client_read(bs_client, &response_len TSRMLS_CC)) == NULL) {
        RETURN_FALSE;
    }
    printf("response: %s \n", response);
    efree(response);
    
    RETURN_TRUE;
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
