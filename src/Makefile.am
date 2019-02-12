include $(top_srcdir)/Makefile.common

lib_LTLIBRARIES = libgnuradio-omnipod.la

libgnuradio_omnipod_la_SOURCES = \
	omnipod_pda.cc \
	utils.cc \
	interface_director.cc

libgnuradio_omnipod_la_LIBADD = \
	$(GNURADIO_CORE_LA)

libgnuradio_omnipod_la_LDFLAGS = $(NO_UNDEFINED) $(LTVERSIONFLAGS)

EXTRA_DIST = \
	     omnipod_pda.h \
	     utils.h \
	     interface_director.h
