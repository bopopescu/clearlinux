AM_CFLAGS = \
	-std=gnu99 \
	-pedantic \
	-Wall \
	-fstack-protector \
	-Wformat \
	-Wformat-security \
	-Wimplicit-function-declaration \
	-Wstrict-prototypes \
	-Wundef \
	-fno-common \
	-Wconversion \
	-Wunreachable-code \
	-funsigned-char \
	-fstack-protector-strong \
	-fPIE \
	-fPIC 


AM_CPPFLAGS = \
	-D_FORTIFY_SOURCE=2 \
	-I $(top_builddir) \
	-I $(top_srcdir) \
	-I $(top_builddir)/src \
	-I $(top_srcdir)/src \
	-DTOPSRCDIR=\"$(top_srcdir)\" \
	-DDATADIR=\"$(datadir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\"

# vim: filetype=automake tabstop=8 shiftwidth=8 noexpandtab
