## Process this file with automake to produce Makefile.in

NULL=

INCLUDES = 							\
	-I$(top_srcdir)/intl 					\
	$(GNOME_INCLUDEDIR)					\
	$(LIBGLADE_CFLAGS)					\
	$(EEL_CFLAGS)						\
	$(ORBIT_CFLAGS)						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps"\"		\
	$(NULL)

bin_PROGRAMS = power-applet

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
	$(ORBIT_LIBS)		\
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
