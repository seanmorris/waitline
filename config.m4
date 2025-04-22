dnl config.m4 for extension php-waitline
if test -z "$SED"; then
  PHP_WAITLINE_SED="sed";
else
  PHP_WAITLINE_SED="$SED";
fi

PHP_ARG_ENABLE([waitline],
  [whether to enable waitline support],
  [AS_HELP_STRING([--enable-waitline],
    [Enable waitline support])],
  [no])

if test "$PHP_WAITLINE" != "no"; then
  tmp_version=$PHP_VERSION
  if test -z "$tmp_version"; then
    if test -z "$PHP_CONFIG"; then
      AC_MSG_ERROR([php-config not found])
    fi
    php_version=`$PHP_CONFIG --version 2>/dev/null|head -n 1|$PHP_WAITLINE_SED -e 's#\([0-9]\.[0-9]*\.[0-9]*\)\(.*\)#\1#'`
  else
    php_version=`echo "$tmp_version"|$PHP_WAITLINE_SED -e 's#\([0-9]\.[0-9]*\.[0-9]*\)\(.*\)#\1#'`
  fi

  ac_IFS=$IFS
  IFS="."
  set $php_version
  IFS=$ac_IFS
  waitline_php_version=`expr [$]1 \* 1000000 + [$]2 \* 1000 + [$]3`

  if test -z "$php_version"; then
    AC_MSG_ERROR([failed to detect PHP version, please report])
  fi

  if test "$waitline_php_version" -lt "8000000"; then
    AC_MSG_ERROR([You need at least PHP 8.0 to be able to use waitline])
  fi

  AC_DEFINE(HAVE_WAITLINE, 1, [ Have waitline support ])

  PHP_NEW_EXTENSION(waitline, waitline.c, $ext_shared)
fi
