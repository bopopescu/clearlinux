#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT(swupd-client, 2.36, timothy.c.pepper@linux.intel.com)
AM_PROG_AR
LT_INIT
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -W])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_LANG(C)
AC_ARG_WITH([swupdcertsdir],
  [AS_HELP_STRING([--with-swupdcertsdir=DIR], [swupdcertsdir files])],
  [swupdcertsdir=$withval],
  [swupdcertsdir="/usr/share/clear/update-ca"])
AC_SUBST([swupdcertsdir], [$swupdcertsdir])
AC_CONFIG_HEADERS([config.h])
PKG_CHECK_MODULES([lzma], [liblzma])
PKG_CHECK_MODULES([zlib], [zlib])
AC_ARG_ENABLE(
	bzip2,
	AS_HELP_STRING([--disable-bzip2],[Do not use bzip2 compression (uses bzip2 by default)]),
	AC_DEFINE(SWUPD_WITHOUT_BZIP2,1,[Do not use bzip2 compression]) ,
	AC_DEFINE(SWUPD_WITH_BZIP2,1,[Use bzip2 compression])
	AC_CHECK_LIB([bz2], [BZ2_bzBuffToBuffCompress], [], [AC_MSG_ERROR([the libbz2 library is missing])])
)
PKG_CHECK_MODULES([curl], [libcurl])
PKG_CHECK_MODULES([openssl], [libcrypto >= 0.9.8])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_PROGS(TAR, tar)
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
