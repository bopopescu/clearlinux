AC_INIT([clr-systemd-config], [1.0], [dimitri.j.ledkov@intel.com])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign -Wall -Werror -Wno-portability])
AC_CONFIG_FILES([Makefile])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
PKG_CHECK_MODULES([SYSTEMD], [systemd])
AC_PATH_PROG([SYSTEMDSYSUSERS],[systemd-sysusers])
sysusersdir=`pkg-config --variable sysusersdir systemd`
AC_SUBST(sysusersdir)
networkdir=`pkg-config --variable systemdutildir systemd`/network
AC_SUBST(networkdir)
udevrulesdir=`pkg-config --variable udevdir udev`/rules.d
AC_SUBST(udevrulesdir)
systemdsystemunitdir=`pkg-config --variable systemdsystemunitdir systemd`
AC_SUBST(systemdsystemunitdir)
systemdsystempresetdir=`pkg-config --variable systemdsystempresetdir systemd`
AC_SUBST(systemdsystempresetdir)
systemdsystempresettransientdir="$systemdsystempresetdir"-transient
AC_SUBST(systemdsystempresettransientdir)
systemdutildir=`pkg-config --variable systemdutildir systemd`
AC_SUBST(systemdutildir)
systemdjournalconfdir="$systemdutildir"/journal.conf.d
AC_SUBST(systemdjournalconfdir)
systemdbootchartconfdir="$systemdutildir"/bootchart.conf.d
AC_SUBST(systemdbootchartconfdir)
defaultsdir="/usr/share/defaults/etc"
AC_SUBST(defaultsdir)
AC_OUTPUT
