# Generated automatically from Makefile.in by configure.
#
# Makefile for Src subdirectory
#
# Copyright (c) 1996 Richard Coleman
# All rights reserved.
#
# Permission is hereby granted, without written agreement and without
# license or royalty fees, to use, copy, modify, and distribute this
# software and to distribute modified versions of this software for any
# purpose, provided that the above copyright notice and the following
# two paragraphs appear in all copies of this software.
#
# In no event shall Richard Coleman or the Zsh Development Group be liable
# to any party for direct, indirect, special, incidental, or consequential
# damages arising out of the use of this software and its documentation,
# even if Richard Coleman and the Zsh Development Group have been advised of
# the possibility of such damage.
#
# Richard Coleman and the Zsh Development Group specifically disclaim any
# warranties, including, but not limited to, the implied warranties of
# merchantability and fitness for a particular purpose.  The software
# provided hereunder is on an "as is" basis, and Richard Coleman and the
# Zsh Development Group have no obligation to provide maintenance,
# support, updates, enhancements, or modifications.
# hand-edited for win32

# zsh version
VERSION = 3.0.8

SHELL = /bin/sh

top_srcdir = ..
srcdir     = .

prefix      = 
exec_prefix = ${prefix}
bindir      = ${exec_prefix}/bin



DEFS       = -DHAVE_CONFIG_H -DDEBUG
windir     = $(srcdir)/win32

include win32/Makerules

INCLUDES = -I$(windir) -I$(windir)/include -I$(srcdir) 

COMPILE = $(CC) -c $(INCLUDES) $(CPPFLAGS) $(DEFS) $(CFLAGS)
LINK    = $(CC) $(LDFLAGS) $(OUTOPT)$@

INSTALL         = .././install-sh -c
INSTALL_PROGRAM = ${INSTALL}

AWK = gawk
SED = sed

.SUFFIXES:
.SUFFIXES: .c .$(oo) .pro

.c.$(oo):
	$(COMPILE) $<

.c.pro:
	$(SED) -n -f $(srcdir)/makepro.sed $< | $(SED) --binary -e "s/\r//" > $@

# this is for ansi2krn conversion
U =

# this header file is parsed to generate signal names (signames.h)
SIGNAL_H = /mingw/include/signal.h

# this header file is parsed to generate limits, if available
RLIMITS_INC_H = /usr/include/sys/resource.h

# headers included in distribution
DIST_HDRS = version.h globals.h hashtable.h prototypes.h signals.h \
system.h zle.h zsh.h ztype.h

# generated headers
GEN_HDRS = win32/signames.h win32/config.h # rlimits.h

# zsh headers necessary for compilation
HDRS = $(DIST_HDRS) $(GEN_HDRS)

# zsh C source
SRCS = 00_entry.c \
builtin.c compat.c cond.c exec.c glob.c hashtable.c hist.c init.c \
input.c jobs.c lex.c linklist.c loop.c math.c mem.c params.c parse.c \
signals.c subst.c text.c utils.c watch.c zle_bindings.c zle_hist.c \
zle_main.c zle_misc.c zle_move.c zle_refresh.c zle_tricky.c zle_utils.c \
zle_vi.c zle_word.c \
fork.c fmalloc.c io.c signal.c \
unistd.c ntermcap.c tconsole.c killnice.c dirent.c access.c fstat.c \
ntdbg.c shell_init.c nt_execve.c stdio.c zsh_support.c \
fullclip.c zz_ntb2.c

# generated prototypes
PROTO = builtin.pro compat.pro cond.pro exec.pro glob.pro hashtable.pro \
hist.pro init.pro input.pro jobs.pro lex.pro linklist.pro loop.pro \
math.pro mem.pro params.pro parse.pro signals.pro subst.pro text.pro \
utils.pro watch.pro zle_hist.pro zle_main.pro \
zle_misc.pro zle_move.pro zle_refresh.pro zle_tricky.pro zle_utils.pro \
zle_vi.pro zle_word.pro

# object files
OBJS = $(notdir $(SRCS:.c=.$(oo))) $(NTRES)

# auxiliary files
AUX = Makefile.in .indent.pro signames.awk rlimits.awk makepro.sed \
      ansi2knr.c

# all files in this directory included in the distribution
DIST = $(DIST_HDRS) $(SRCS) $(AUX)

# ========= DEPENDENCIES FOR BUILDING ==========

# default target
all: zsh

zsh: $(OBJS)
	$(LINK) $(OBJS) $(LIBS) $(CLFLAGS)
ifdef MSC_MFT
	mt.exe -nologo -manifest $(windir)/zsh.exe.manifest -outputresource:"zsh.exe;1"
endif	

sh: $(OBJS) $(NTRES)
	$(LINK) $(OBJS) $(LIBS) $(CLFLAGS)
ifdef MSC_MFT
	mt.exe -nologo -manifest $(windir)/zsh.exe.manifest -outputresource:"sh.exe;1"
endif	

# special build rules
fork.$(oo): $(windir)/fork.c
	$(COMPILE) $(SPECIALCFLAG) $(windir)/$*.c

exec.$(oo): exec.c
	$(COMPILE) $(NOOPTIMIZE) $*.c

# resource rule
$(NTRES): $(windir)/zsh.rc
	$(RC) $(RCOPTS) $(windir)/zsh.rc $(OUTNTRES)

ansi2knr: ansi2knr.o
	$(LINK) ansi2knr.o

signames.h: signames.awk
	$(AWK) -f $(srcdir)/signames.awk $(SIGNAL_H) > signames.h

prototypes.h: $(PROTO)

# this file will not be made if limits are unavailable:
# silent so the warning doesn't appear unless necessary
#rlimits.h: rlimits.awk $(RLIMITS_INC_H)
#	@echo '$(AWK) -f $(srcdir)/rlimits.awk $(RLIMITS_INC_H) > rlimits.h'; \
#	$(AWK) -f $(srcdir)/rlimits.awk $(RLIMITS_INC_H) > rlimits.h

$(OBJS): $(HDRS)

$(PROTO): makepro.sed

# ========== DEPENDENCIES FOR INSTALLING ==========

install: install.bin

uninstall: uninstall.bin

# install binary, creating install directory if necessary
install.bin: zsh
	$(top_srcdir)/mkinstalldirs $(bindir)
	-if [ -f $(bindir)/zsh ]; then mv $(bindir)/zsh $(bindir)/zsh.old; fi
	$(INSTALL_PROGRAM) zsh $(bindir)/zsh
	-if [ -f $(bindir)/zsh-$(VERSION) ]; then rm -f $(bindir)/zsh-$(VERSION); fi
	ln $(bindir)/zsh $(bindir)/zsh-$(VERSION)

# uninstall binary
uninstall.bin:
	-if [ -f $(bindir)/zsh ]; then rm -f $(bindir)/zsh; fi
	-if [ -f $(bindir)/zsh-$(VERSION) ]; then rm -f $(bindir)/zsh-$(VERSION); fi

# ========== DEPENDENCIES FOR ANSI TO KNR CONVERSION ==========

_builtin.c: builtin.c ansi2knr
	./ansi2knr $(srcdir)/builtin.c > _builtin.c
_compat.c: compat.c ansi2knr
	./ansi2knr $(srcdir)/compat.c > _compat.c
_cond.c: cond.c ansi2knr
	./ansi2knr $(srcdir)/cond.c > _cond.c
_exec.c: exec.c ansi2knr
	./ansi2knr $(srcdir)/exec.c > _exec.c
_glob.c: glob.c ansi2knr
	./ansi2knr $(srcdir)/glob.c > _glob.c
_hashtable.c: hashtable.c ansi2knr
	./ansi2knr $(srcdir)/hashtable.c > _hashtable.c
_hist.c: hist.c ansi2knr
	./ansi2knr $(srcdir)/hist.c > _hist.c
_init.c: init.c ansi2knr
	./ansi2knr $(srcdir)/init.c > _init.c
_input.c: input.c ansi2knr
	./ansi2knr $(srcdir)/input.c > _input.c
_jobs.c: jobs.c ansi2knr
	./ansi2knr $(srcdir)/jobs.c > _jobs.c
_lex.c: lex.c ansi2knr
	./ansi2knr $(srcdir)/lex.c > _lex.c
_linklist.c: linklist.c ansi2knr
	./ansi2knr $(srcdir)/linklist.c > _linklist.c
_loop.c: loop.c ansi2knr
	./ansi2knr $(srcdir)/loop.c > _loop.c
_math.c: math.c ansi2knr
	./ansi2knr $(srcdir)/math.c > _math.c
_mem.c: mem.c ansi2knr
	./ansi2knr $(srcdir)/mem.c > _mem.c
_params.c: params.c ansi2knr
	./ansi2knr $(srcdir)/params.c > _params.c
_parse.c: parse.c ansi2knr
	./ansi2knr $(srcdir)/parse.c > _parse.c
_signals.c: signals.c ansi2knr
	./ansi2knr $(srcdir)/signals.c > _signals.c
_subst.c: subst.c ansi2knr
	./ansi2knr $(srcdir)/subst.c > _subst.c
_text.c: text.c ansi2knr
	./ansi2knr $(srcdir)/text.c > _text.c
_utils.c: utils.c ansi2knr
	./ansi2knr $(srcdir)/utils.c > _utils.c
_watch.c: watch.c ansi2knr
	./ansi2knr $(srcdir)/watch.c > _watch.c
_zle_bindings.c: zle_bindings.c ansi2knr
	./ansi2knr $(srcdir)/zle_bindings.c > _zle_bindings.c
_zle_hist.c: zle_hist.c ansi2knr
	./ansi2knr $(srcdir)/zle_hist.c > _zle_hist.c
_zle_main.c: zle_main.c ansi2knr
	./ansi2knr $(srcdir)/zle_main.c > _zle_main.c
_zle_misc.c: zle_misc.c ansi2knr
	./ansi2knr $(srcdir)/zle_misc.c > _zle_misc.c
_zle_move.c: zle_move.c ansi2knr
	./ansi2knr $(srcdir)/zle_move.c > _zle_move.c
_zle_refresh.c: zle_refresh.c ansi2knr
	./ansi2knr $(srcdir)/zle_refresh.c > _zle_refresh.c
_zle_tricky.c: zle_tricky.c ansi2knr
	./ansi2knr $(srcdir)/zle_tricky.c > _zle_tricky.c
_zle_utils.c: zle_utils.c ansi2knr
	./ansi2knr $(srcdir)/zle_utils.c > _zle_utils.c
_zle_vi.c: zle_vi.c ansi2knr
	./ansi2knr $(srcdir)/zle_vi.c > _zle_vi.c
_zle_word.c: zle_word.c ansi2knr
	./ansi2knr $(srcdir)/zle_word.c > _zle_word.c

# ========== DEPENDENCIES FOR CLEANUP ==========

mostlyclean:
	rm -f core *.o *~

clean: 
	-@cmd /C "del /S /F /Q *.exe *.obj *.o *.pch *.map *.pdb *.ilk *.ncb *.res"

distclean: clean
	rm -f Makefile

realclean: distclean
	rm -f TAGS tags

superclean: realclean

# ========== DEPENDENCIES FOR MAINTENANCE ==========

subdir = Src

Makefile: Makefile.in ../config.status
	cd .. && CONFIG_FILES=$(subdir)/$@ CONFIG_HEADERS= ./config.status

# tag file for vi
tags: TAGS
	cd $(srcdir) && ctags -w $(SRCS) $(DIST_HDRS)

# tag file for emacs
eTAGS: $(SRCS) $(DIST_HDRS)
	cd $(srcdir) && etags $(SRCS) $(DIST_HDRS)

distdir = ../`cat ../distname`/$(subdir)
dist: $(DIST)
	@echo "Copying distribution files in $(subdir)"
	for file in $(DIST); do    \
	  cp -p $$file $(distdir); \
	done
