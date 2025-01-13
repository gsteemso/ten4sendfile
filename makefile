# Makefile for ten4sendfile
# written 2025-Jan-11 by gsteemso

CC      := gcc-4.2
CFLAGS  := -Os
CP      := cp -P
MKDIR_P := mkdir -p

prefix     := .
libdir     := $(prefix)/lib
includedir := $(prefix)/include
mandir     := $(prefix)/share/man
man2dir    := $(mandir)/man2

lib_source := ten4sendfile.c
lib_header := ten4sendfile.h
manpage    := sendfile.2.gz

built_lib  := libsendfile.dylib
inst_hdr   := sendfile.h

installed_lib := $(libdir)/$(built_lib)
installed_hdr := $(includedir)/$(inst_hdr)
installed_man := $(man2dir)/$(manpage)
installed     := $(installed_lib) $(installed_hdr) $(installed_man)

dylib_vers_args := -compatibility_version 1.0.0 -current_version 1.0.0
dylib_args := -dynamiclib -install_name $(installed_lib) -headerpad_max_install_names $(dylib_vers_args)
Tiger_bin_args := -undefined dynamic_lookup

export MACOSX_DEPLOYMENT_TARGET := 10.3

$(built_lib) : $(lib_header) $(lib_source)
	$(CC) $(CFLAGS) $(dylib_args) $(Tiger_bin_args) -o $(built_lib) $(lib_source)

install : $(built_lib) $(lib_header) $(manpage)
	@$(MKDIR_P) $(includedir)
	@$(MKDIR_P) $(libdir)
	@$(MKDIR_P) $(man2dir)
	$(CP) $(built_lib) $(installed_lib)
	$(CP) $(lib_header) $(installed_hdr)
	$(CP) $(manpage) $(installed_man)

uninstall :
	@function rmdir_if_possible {        \
	  if [ $$1 != $${1%/*} -a -d $$1 ] ; \
	  then rmdir $$1 &&                  \
	      rmdir_if_possible $${1%/*} ;   \
	  fi ;                               \
	} ;                                  \
	for f in $(installed) ; do           \
	    test -e $$f && $(RM) $$f ;       \
	    rmdir_if_possible $${f%/*} ;     \
	done ;                               \
	echo 'Uninstallation was successful.'

clean :
	$(RM) $(built_lib)

help :
	@echo ''
	@echo 'This library implements sendfile(2), which Apple managed to leave out of Mac OS'
	@echo 'prior to version 10.5, despite listing it in the system headers (incorrectly).'
	@echo ''
	@echo 'To install software that requires sendfile(2) on Mac OS 10.3.9/10.4.x, install'
	@echo 'this library (plus its header file and manpage).  Where everything goes is con-'
	@echo 'trolled by these command-line variables:'
	@echo ''
	@echo '    Variable        Default Setting'
	@echo '    prefix          . (the current directory)'
	@echo '    libdir          $${prefix}/lib'
	@echo '    includedir      $${prefix}/include'
	@echo '    mandir          $${prefix}/share/man'
	@echo '    man2dir         $${mandir}/man2'
	@echo ''
	@echo 'Simply enter `make install` at the command prompt.  Other Make targets are the'
	@echo "default (builds the library but doesn't install it); `clean` (deletes the built"
	@echo 'library from the build directory); and `uninstall` (erases all installed files,'
	@echo 'plus any empty directories left by their removal).'
	@echo ''
	@echo 'Software you want to install must add "-I$${includedir} -L$${libdir} -lsendfile"'
	@echo 'to its compiler command line(s), and replace "#include <sys/socket.h>" with'
	@echo '"#include "sendfile.h"" in any files where sendfile(2) is used.  "sendfile.h"'
	@echo 'safely includes <sys/socket.h> without interfering with itself.'

.PHONY : clean help install uninstall
