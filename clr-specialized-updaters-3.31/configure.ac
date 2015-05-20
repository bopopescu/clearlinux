
AC_PREREQ([2.68])
AC_INIT(kernel_updater, 3.06, brad.t.peters@intel.com)
AM_INIT_AUTOMAKE([foreign silent-rules color-tests no-dist-gzip dist-xz])
AC_CONFIG_FILES([Makefile])

PKG_PROG_PKG_CONFIG

# Options
AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
    [path to systemd system service directory]), [path_systemdsystemunit=${withval}],
    [path_systemdsystemunit="`$PKG_CONFIG --variable=systemdsystemunitdir systemd`"])
SYSTEMD_SYSTEMUNITDIR="${path_systemdsystemunit}"
AC_SUBST(SYSTEMD_SYSTEMUNITDIR)
AM_CONDITIONAL(SYSTEMD, test -n "${path_systemdsystemunit}")

AC_OUTPUT
