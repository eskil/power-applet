## Process this file with automake to produce Makefile.in

NULL=

INCLUDES = 							\
	-I$(top_srcdir)/intl 					\
	$(GNOME_INCLUDEDIR)					\
	$(LIBGLADE_CFLAGS)					\
	$(EEL_CFLAGS)						\
	$(GNOME_VFS_CFLAGS)					\
	$(GDK_PIXBUF_CFLAGS)					\
	$(ORBIT_CFLAGS)						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps"\"		\
	$(NULL)

bin_PROGRAMS = power-applet
lib_LTLIBRARIES = libpowerm.la
noinst_PROGRAMS = test-libpowerm

libpowerm_la_LDFLAGS =		\
	$(NULL)

libpowerm_la_SOURCES =		\
	powerm.c		\
	powerm.h		\
	$(NULL)

libpowerm_la_LDFLAGS =		\
	-version-info 0:0:0	\
	$(NULL)

test_libpowerm_SOURCES =	\
	test-libpowerm.c	\
	$(NULL)

test_libpowerm_LDADD = 		\
	libpowerm.la		\
	$(NULL)

power_applet_SOURCES = 	\
	power-applet.c	\
	$(NULL)

power_applet_LDADD = 		\
	$(GNOME_LIBDIR) 	\
	$(GNOMEUI_LIBS) 	\
	$(GNOME_APPLETS_LIBS)	\
	$(INTLLIBS)		\
	$(LIBGLADE_LIBS)	\
	$(EEL_LIBS)		\
	$(GNOME_VFS_CFLAGS)	\
	$(GDK_PIXBUF_CFLAGS)	\
	$(ORBIT_LIBS)		\
	libpowerm.la		\
	$(NULL)

sysdir = $(datadir)/applets/Monitors
sys_DATA = power-applet.desktop

gladedir = $(datadir)/$(PACKAGE)
glade_DATA = power-applet.glade

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = power-applet.png

gnorbadir = $(sysconfdir)/CORBA/servers
gnorba_DATA = power-applet.gnorba

EXTRA_DIST = 				\
	$(glade_DATA)			\
	$(sys_DATA)			\
	$(pixmap_DATA)			\
	$(gnorba_DATA)			\
	power-applet-strings.c	\
	$(NULL)
