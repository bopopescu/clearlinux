TESTS = ascii.t iso.t utf8.t utf16.t misc.t

d2u_os=$(shell uname -s)

# Checking WIN32 version in MSYS shell.
ifeq ($(findstring MINGW,$(d2u_os)),MINGW)
TESTS += gb18030.t
endif

# Unix/Linux/Cygwin. Check if zh_CN.gb18030 locale is supported.
ifeq ($(shell ./chk_loc.sh zh_CN.gb18030),yes)
TESTS += gb18030.t
endif

# Windows version does not support symlinks like the Unix version.
ifneq ($(findstring MINGW,$(d2u_os)),MINGW)
TESTS += symlink.t
endif


all: test

check: test

test:
	prove -v $(TESTS)


ascii:
	prove -v $@.t

iso:
	prove -v $@.t

misc:
	prove -v $@.t

utf8:
	prove -v $@.t

utf16:
	prove -v $@.t

gb18030:
	prove -v $@.t

symlink:
	prove -v $@.t

clean:
	rm -f out*.txt in_link.txt
