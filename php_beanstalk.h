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

#ifndef PHP_BEANSTALK_H
#define PHP_BEANSTALK_H

extern zend_module_entry beanstalk_module_entry;
#define phpext_beanstalk_ptr &beanstalk_module_entry

#define PHP_BEANSTALK_VERSION "0.0.1"

#ifdef PHP_WIN32
#	define PHP_BEANSTALK_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_BEANSTALK_API __attribute__ ((visibility("default")))
#else
#	define PHP_BEANSTALK_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(beanstalk);
PHP_MSHUTDOWN_FUNCTION(beanstalk);
PHP_RINIT_FUNCTION(beanstalk);
PHP_RSHUTDOWN_FUNCTION(beanstalk);
PHP_MINFO_FUNCTION(beanstalk);

PHP_METHOD(Beanstalk, __construct);
PHP_METHOD(Beanstalk, connect);
PHP_METHOD(Beanstalk, close);
PHP_METHOD(Beanstalk, put);
//PHP_METHOD(Beanstalk, useTube);

typedef struct _beanstalk_client {
    php_stream     *stream;
    char           *host;
    unsigned short port;
    long           timeout;
    //int            failed;
    int            status;
} beanstalk_client;

#define BEANSTALK_CLIENT_STATUS_FAILED 0
#define BEANSTALK_CLIENT_STATUS_DISCONNECTED 1
#define BEANSTALK_CLIENT_STATUS_UNKNOWN 2
#define BEANSTALK_CLIENT_STATUS_CONNECTED 3

/* 
  	Declare any global variables you may need between the BEGIN
	and END macros here:     

ZEND_BEGIN_MODULE_GLOBALS(beanstalk)
	long  global_value;
	char *global_string;
ZEND_END_MODULE_GLOBALS(beanstalk)
*/

#ifdef ZTS
#define BEANSTALK_G(v) TSRMG(beanstalk_globals_id, zend_beanstalk_globals *, v)
#else
#define BEANSTALK_G(v) (beanstalk_globals.v)
#endif

#endif	/* PHP_BEANSTALK_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
