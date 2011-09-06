/* GStreamer
 * Copyright (C) 2004-6 Davide Bertola <dade at dadeb dot it>
 *
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * The development of this code was made possible due to the involvement of
 * Pioneers of the Inevitable, the creators of the Songbird Music player.
 * 
 */

/**
 * SECTION:element-calayersink
 *
 * The CALayerSink renders video frames to a MacOSX CoreAnimation Layer. 
 * The video output must be directed to a window embedded in an existing NSApp.
 *
 * When the NSView to be embedded is created an element #GstMessage with a 
 * name of 'have-ca-layer' will be created and posted on the bus. 
 * The pointer to the NSView to embed will be in the 'nsview' field of that 
 * message. The application MUST handle this message and embed the view
 * appropriately.
 */

#include "config.h"
#include <gst/interfaces/xoverlay.h>

#include "calayersink.h"
#import "GstCAOpenGLLayer.h"
#include <unistd.h>

GST_DEBUG_CATEGORY (gst_debug_ca_layer_sink);
#define GST_CAT_DEFAULT gst_debug_ca_layer_sink

static GstStaticPadTemplate gst_ca_layer_sink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ], "
#if G_BYTE_ORDER == G_BIG_ENDIAN
       "format = (fourcc) YUY2")
#else
        "format = (fourcc) UYVY")
#endif
    );

enum
{
  ARG_0,
  ARG_EMBED,
};

static void gst_ca_layer_sink_layer_destroy (GstCALayerSink * calayersink);
static void
gst_ca_layer_sink_layer_reparent (CALayer *layer, CALayer *parent);

static GstVideoSinkClass *parent_class = NULL;

static gboolean
gst_ca_layer_sink_layer_create (GstCALayerSink * calayersink, gint width,
    gint height)
{
  GstStructure *s;
  GstMessage *msg;
  gboolean res = TRUE;

  g_return_val_if_fail (GST_IS_CA_LAYER_SINK (calayersink), FALSE);
  GST_DEBUG_OBJECT (calayersink, "Creating new CALayer");

  calayersink->layer = [[[GstCAOpenGLLayer alloc] init] retain];
  
  if (calayersink->parent_layer)
    gst_ca_layer_sink_layer_reparent (calayersink->layer,
        calayersink->parent_layer);

  [calayersink->layer setNeedsDisplay];
  s = gst_structure_new ("have-ca-layer",
     "calayer", G_TYPE_POINTER, calayersink->layer,
     nil);

  msg = gst_message_new_element (GST_OBJECT (calayersink), s);
  gst_element_post_message (GST_ELEMENT (calayersink), msg);

  GST_INFO_OBJECT (calayersink, "'have-ca-layer' message sent");

  return res;
}

static void
gst_ca_layer_sink_layer_destroy (GstCALayerSink * calayersink)
{
  g_return_if_fail (GST_IS_CA_LAYER_SINK (calayersink));
  [calayersink->layer removeFromSuperlayer];
  [calayersink->layer release];
  calayersink->layer = NULL;
}

static void
gst_ca_layer_sink_layer_reparent (CALayer *layer, CALayer *parent_layer) {
  layer.bounds = parent_layer.bounds; 
  layer.frame = parent_layer.frame; 
  layer.autoresizingMask = 
    kCALayerWidthSizable|kCALayerHeightSizable;
  [parent_layer addSublayer: layer];
  //layer.backgroundColor = CGColorCreateGenericRGB (0.0, 0.0, 1.0, 1.0);
  [parent_layer setNeedsDisplay];
}

static gboolean
gst_ca_layer_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstCALayerSink *calayersink;
  GstStructure *structure;
  gboolean res, result = FALSE;
  gint video_width, video_height;

  calayersink = GST_CA_LAYER_SINK (bsink);

  GST_DEBUG_OBJECT (calayersink, "caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  res = gst_structure_get_int (structure, "width", &video_width);
  res &= gst_structure_get_int (structure, "height", &video_height);

  if (!res) {
    goto beach;
  }

  GST_DEBUG_OBJECT (calayersink, "our format is: %dx%d video",
      video_width, video_height);

  GST_VIDEO_SINK_WIDTH (calayersink) = video_width;
  GST_VIDEO_SINK_HEIGHT (calayersink) = video_height;
  
  [calayersink->layer setVideoSize: video_width: video_height];

  result = TRUE;

beach:
  return result;

}

static GstStateChangeReturn
gst_ca_layer_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstCALayerSink *calayersink;
  GstStateChangeReturn ret;

  calayersink = GST_CA_LAYER_SINK (element);

  GST_DEBUG_OBJECT (calayersink, "%s => %s", 
        gst_element_state_get_name(GST_STATE_TRANSITION_CURRENT (transition)),
        gst_element_state_get_name(GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_VIDEO_SINK_WIDTH (calayersink) = 320;
      GST_VIDEO_SINK_HEIGHT (calayersink) = 240;
      if (!gst_ca_layer_sink_layer_create (calayersink,
          GST_VIDEO_SINK_WIDTH (calayersink),
          GST_VIDEO_SINK_HEIGHT (calayersink))) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = (GST_ELEMENT_CLASS (parent_class))->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_VIDEO_SINK_WIDTH (calayersink) = 0;
      GST_VIDEO_SINK_HEIGHT (calayersink) = 0;
      gst_ca_layer_sink_layer_destroy (calayersink);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

done:
  return ret;
}

static GstFlowReturn
gst_ca_layer_sink_show_frame (GstBaseSink * bsink, GstBuffer *buf)
{
  GstCALayerSink *calayersink;
  calayersink = GST_CA_LAYER_SINK (bsink);

  [calayersink->layer setTextureBuffer: (void *) GST_BUFFER_DATA (buf)];
  //gst_buffer_ref (buf);
  printf ("%p\n", GST_BUFFER_DATA (buf));
  [calayersink->layer display];
  [CATransaction flush];

  return GST_FLOW_OK;
}

static void
gst_ca_layer_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCALayerSink *calayersink;

  g_return_if_fail (GST_IS_CA_LAYER_SINK (object));

  calayersink = GST_CA_LAYER_SINK (object);

  switch (prop_id) {
    case ARG_EMBED:
      /* Ignore, just here for backwards compatibility */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ca_layer_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCALayerSink *calayersink;

  g_return_if_fail (GST_IS_CA_LAYER_SINK (object));

  calayersink = GST_CA_LAYER_SINK (object);

  switch (prop_id) {
    case ARG_EMBED:
      g_value_set_boolean (value, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_ca_layer_sink_init (GstCALayerSink * calayersink)
{
  calayersink->layer = NULL;
  calayersink->parent_layer = NULL;
}

static void
gst_ca_layer_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "OSX CoreAnimation video sink",
      "Sink/Video", "OSX CoreAnimation videosink",
      "Davide Bertola <dade at dadeb dot it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ca_layer_sink_sink_template_factory));
}

static void
gst_ca_layer_sink_finalize (GObject *object)
{
  GstCALayerSink *calayersink = GST_CA_LAYER_SINK (object);

  if (calayersink->layer)
    [calayersink->layer release];

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ca_layer_sink_class_init (GstCALayerSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;


  parent_class = g_type_class_ref (GST_TYPE_VIDEO_SINK);

  gobject_class->set_property = gst_ca_layer_sink_set_property;
  gobject_class->get_property = gst_ca_layer_sink_get_property;
  gobject_class->finalize = gst_ca_layer_sink_finalize;

  gstbasesink_class->set_caps = gst_ca_layer_sink_setcaps;
  gstbasesink_class->preroll = gst_ca_layer_sink_show_frame;
  gstbasesink_class->render = gst_ca_layer_sink_show_frame;
  gstelement_class->change_state = gst_ca_layer_sink_change_state;

  g_object_class_install_property (gobject_class, ARG_EMBED,
      g_param_spec_boolean ("embed", "embed", "For ABI compatiblity only, do not use",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static gboolean
gst_ca_layer_sink_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_ca_layer_sink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_ca_layer_sink_interface_supported;
}

static void
gst_ca_layer_sink_set_window_handle (GstXOverlay * overlay, guintptr handle_id)
{
  GstCALayerSink *calayersink = GST_CA_LAYER_SINK (overlay);
  calayersink->parent_layer = (CALayer *) handle_id;

  if (calayersink->layer) 
    gst_ca_layer_sink_layer_reparent (calayersink->layer,
        calayersink->parent_layer);
}

static void
gst_ca_layer_sink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_ca_layer_sink_set_window_handle;
  iface->expose = NULL;
  iface->handle_events = NULL;
}

GType
gst_ca_layer_sink_get_type (void)
{
  static GType calayersink_type = 0;

  if (!calayersink_type) {
    static const GTypeInfo calayersink_info = {
      sizeof (GstCALayerSinkClass),
      gst_ca_layer_sink_base_init,
      NULL,
      (GClassInitFunc) gst_ca_layer_sink_class_init,
      NULL,
      NULL,
      sizeof (GstCALayerSink),
      0,
      (GInstanceInitFunc) gst_ca_layer_sink_init,
    };

    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_ca_layer_sink_interface_init,
      NULL,
      NULL,
    };

    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_ca_layer_sink_xoverlay_init,
      NULL,
      NULL,
    };

    calayersink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstCALayerSink", &calayersink_info, 0);

    g_type_add_interface_static (calayersink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (calayersink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  return calayersink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "calayersink",
          GST_RANK_PRIMARY, GST_TYPE_CA_LAYER_SINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_ca_layer_sink, "calayersink", 0,
      "calayersink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "calayersink",
    "OSX CoreAnimation video output plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
