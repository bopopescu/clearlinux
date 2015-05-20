#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([telemetrics-client], [1.2.0], [https://clearlinux.org/])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.14 -Wall -Werror -Wno-extra-portability foreign subdir-objects])
AM_SILENT_RULES([yes])
LT_INIT([disable-static])
AC_CONFIG_MACRO_DIRS([build-aux/m4])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
PKG_CHECK_MODULES([CHECK], [check >= 0.9.12])
PKG_CHECK_MODULES([CURL], [libcurl])
PKG_CHECK_MODULES([GLIB], [glib-2.0])
AC_CHECK_LIB([elf], [elf_begin], [have_elflib=yes], [AC_MSG_ERROR([Unable to find libelf from elfutils])])
AC_CHECK_LIB([dw], [dwfl_begin], [have_dwlib=yes], [AC_MSG_ERROR([Unable to find libdw from elfutils])])
AS_IF([test "x$have_elflib" = "xyes" -a "x$have_dwlib" = "xyes"],
      [AC_SUBST(ELFUTILS_LIBS, "-lelf -ldw")])

# Special checks for systemd libs
TELEM_CHECK_SYSTEMD([journal],[JOURNAL])
TELEM_CHECK_SYSTEMD([daemon],[DAEMON])
TELEM_CHECK_SYSTEMD([id128],[ID128])

# Checks for header files.
AC_CHECK_HEADERS([curl/curl.h])
AC_CHECK_HEADERS([elfutils/libdwfl.h])
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([getopt.h])
AC_CHECK_HEADERS([inttypes.h])
AC_CHECK_HEADERS([libelf.h])
AC_CHECK_HEADERS([limits.h])
AC_CHECK_HEADERS([stdarg.h])
AC_CHECK_HEADERS([stdint.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([string.h])
AC_CHECK_HEADERS([sys/fcntl.h])
AC_CHECK_HEADERS([sys/queue.h])
AC_CHECK_HEADERS([sys/prctl.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([sys/types.h])
AC_CHECK_HEADERS([syslog.h])
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memmove])
AC_CHECK_FUNCS([memset])
AC_CHECK_FUNCS([socket])
AC_CHECK_FUNCS([strdup])
AC_CHECK_FUNCS([strerror])
AC_CHECK_FUNCS([strndup])
AC_CHECK_FUNCS([strtol])
AC_CHECK_FUNCS([uname])

# Options

AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
	    [path to systemd system service dir]), [unitpath=${withval}],
	    [unitpath="$($PKG_CONFIG --variable=systemdsystemunitdir systemd)"])
test -z "${unitpath}" && unitpath=/usr/lib/systemd/system
AC_SUBST(SYSTEMD_UNITDIR, [${unitpath}])

AC_ARG_WITH([socketdir], AS_HELP_STRING([--with-socketdir=DIR],
	    [path to dir for telemetrics socket]), [socketpath=${withval}],
	    [socketpath="/run"])
AC_SUBST(SOCKETDIR, [${socketpath}])


AC_CONFIG_COMMANDS([config dir],[mkdir -p src/data; touch src/data/.dirstamp])
AC_CONFIG_FILES([Makefile])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_OUTPUT
