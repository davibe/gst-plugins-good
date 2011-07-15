
plugin_LTLIBRARIES = libgstcalayersink.la

libgstcalayersink_la_SOURCES = calayersink.m GstCAOpenGLLayer.m
libgstcalayersink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstcalayersink_la_LIBADD =  \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR)

libgstcalayersink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework -Wl,Quartz -Wl,-framework -Wl,QuickTime -Wl,-framework -Wl,OpenGL
libgstcalayersink_la_LIBTOOLFLAGS = --tag=disable-static

AM_OBJCFLAGS=$(CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) -Wno-aggregate-return

noinst_HEADERS = calayersink.h GstCAOpenGLLayer.h
