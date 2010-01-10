dnl $Id$
dnl config.m4 for extension beanstalk

PHP_ARG_ENABLE(beanstalk, whether to enable beanstalk support,
[  --enable-beanstalk           Enable beanstalk support])

if test "$PHP_BEANSTALK" != "no"; then
  PHP_NEW_EXTENSION(beanstalk, beanstalk.c, $ext_shared)
fi
