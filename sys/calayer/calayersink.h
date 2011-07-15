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
 *
 * The development of this code was made possible due to the involvement of Pioneers 
 * of the Inevitable, the creators of the Songbird Music player
 * 
 */
 
#ifndef __GST_CA_LAYER_SINK_H__
#define __GST_CA_LAYER_SINK_H__

#include <gst/video/gstvideosink.h>

#include <string.h>
#include <math.h>

#import "GstCAOpenGLLayer.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_ca_layer_sink);
#define GST_CAT_DEFAULT gst_debug_ca_layer_sink

G_BEGIN_DECLS

#define GST_TYPE_CA_LAYER_SINK \
  (gst_ca_layer_sink_get_type())
#define GST_CA_LAYER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CA_LAYER_SINK, GstCALayerSink))
#define GST_CA_LAYER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CA_LAYER_SINK, GstCALayerSinkClass))
#define GST_IS_CA_LAYER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CA_LAYER_SINK))
#define GST_IS_CA_LAYER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CA_LAYER_SINK))

typedef struct _GstCALayerSink GstCALayerSink;
typedef struct _GstCALayerSinkClass GstCALayerSinkClass;

#define GST_TYPE_OSXVIDEOBUFFER (gst_calayerbuffer_get_type())

struct _GstCALayerSink {
  GstVideoSink videosink;
  GstCAOpenGLLayer *layer;
  CALayer *parent_layer;
};

struct _GstCALayerSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_ca_layer_sink_get_type(void);

G_END_DECLS

#endif /* __GST_CA_LAYER_SINK_H__ */

