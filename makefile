# Makefile for ten4sendfile.
# Written 2025-Jan-11, last edited 2025-Oct-18 by gsteemso.

CC        := cc
CFLAGS    := -std=gnu99 -Os
CP        := cp -p
MKDIR_P   := mkdir -p
RM        := rm -f
ifdef W
warnflags := -Wall -Wextra
else
warnflags :=
endif

prefix     := .
libdir     := $(prefix)/lib
includedir := $(prefix)/include
mandir     := $(prefix)/share/man
man2dir    := $(mandir)/man2

lib_source := ten4sendfile.c
lib_header := ten4sendfile.h
manpage    := sendfile.2.gz

built_lib  := libsendfile.dylib
inst_hdr   := sys/socket.h

installed_lib := $(libdir)/$(built_lib)
installed_hdr := $(includedir)/$(inst_hdr)
installed_man := $(man2dir)/$(manpage)
installed     := $(installed_lib) $(installed_hdr) $(installed_man)

dylib_vers_args := -compatibility_version 1.0.0 -current_version 1.0.0
dylib_args := -dynamiclib -install_name $(installed_lib) -headerpad_max_install_names $(dylib_vers_args)

export MACOSX_DEPLOYMENT_TARGET := 10.3

$(built_lib) : $(lib_header) $(lib_source)
	$(CC) $(CFLAGS) $(warnflags) $(dylib_args) -o $(built_lib) $(lib_source)

install : $(built_lib) $(lib_header) $(manpage)
	@$(MKDIR_P) $(includedir)/sys
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
	@echo 'until release 10.5, despite it appearing in earlier system headers (albeit with'
	@echo 'an incorrect prototype).  To allow software builds requiring sendfile(2) on Mac'
	@echo 'OS 10.3.9 or 10.4.x, install this library and its header file (& manpage).  The'
	@echo 'installed header is named "sys/socket.h", and it incorporates the stock version'
	@echo 'thereof as part of its normal operations.'
	@echo ''
	@echo 'Where everything gets put depends on these command-line variables:'
	@echo '    Variable        Default Setting'
	@echo '   ----------      -----------------'
	@echo '    prefix          . (the current directory)'
	@echo '    libdir          $${prefix}/lib'
	@echo '    includedir      $${prefix}/include'
	@echo '    mandir          $${prefix}/share/man'
	@echo '    man2dir         $${mandir}/man2'
	@echo ''
	@echo 'Just command `make install`, including any variables you need to change.  Other'
	@echo 'possible Make targets are the default (which builds the library, not installing'
	@echo 'it); `clean` (which deletes the built but not installed library); & `uninstall`'
	@echo '(which deletes all installed files, plus any empty directories they leave).  To'
	@echo 'build other software, you then add whichever of "-isystem $${includedir}" and/or'
	@echo '"-L$${libdir} -lsendfile" should apply to the relevant compiler command line(s).'
	@echo ''

.PHONY : clean help install uninstall
