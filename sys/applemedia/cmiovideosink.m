/*
 * GStreamer
 * Copyright (C) 2020 Chris Hiszpanski <chris@hiszpanski.name>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-cmiovideosink
 *
 * cmiovideosink renders video frames to a CoreMediaIO Device Abstraction
 * Layer (DAL) plug-in based virtual camera device that can be used by
 * third-party applications.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cmiovideosink.h"

GST_DEBUG_CATEGORY (gst_debug_av_sink);
#define GST_CAT_DEFAULT gst_debug_av_sink

static void gst_cmio_video_sink_finalize (GObject * object);
static void gst_cmio_video_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_cmio_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_cmio_video_sink_start (GstBaseSink * bsink);
static gboolean gst_cmio_video_sink_stop (GstBaseSink * bsink);

static void gst_cmio_video_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_cmio_video_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps * gst_cmio_video_sink_get_caps (GstBaseSink * bsink, GstCaps * filter);
static GstFlowReturn gst_cmio_video_sink_prepare (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_cmio_video_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buf);
static gboolean gst_cmio_video_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

static GstStaticPadTemplate gst_cmio_video_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, ARGB, BGRA, ABGR, RGBA, YUY2, UYVY, NV12, I420 }"))
    );

enum
{
  PROR_0,
  PROP_NAME,
};

#define gst_cmio_video_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCMIOVideoSink, gst_cmio_video_sink,
    GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT (gst_debug_av_sink,
    	"cmiovideosink", 0, "CoreMediaIO Video Sink"));

static void
gst_cmio_video_sink_class_init (GstCMIOVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_cmio_video_sink_set_property;
  gobject_class->get_property = gst_cmio_video_sink_get_property;

  gst_element_class_set_metadata (element_class, "CMIO video sink",
      "Sink/Video", "CoreMediaIO virtual camera device sink",
      "Chris Hiszpanski <chris@hiszpanski.name>");

  gst_element_class_add_static_pad_template (element_class, &gst_cmio_video_sink_template);

  gobject_class->finalize = gst_cmio_video_sink_finalize;

  gstbasesink_class->get_caps = gst_cmio_video_sink_get_caps;
  gstbasesink_class->set_caps = gst_cmio_video_sink_set_caps;
  gstbasesink_class->get_times = gst_cmio_video_sink_get_times;
  gstbasesink_class->prepare = gst_cmio_video_sink_prepare;
  gstbasesink_class->propose_allocation = gst_cmio_video_sink_propose_allocation;
  gstbasesink_class->stop = gst_cmio_video_sink_stop;
  gstbasesink_class->start = gst_cmio_video_sink_start;

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_cmio_video_sink_show_frame);
}

static void
gst_cmio_video_sink_init (GstCMIOVideoSink * av_sink)
{
}

static void
gst_cmio_video_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCMIOVideoSink *cmio_sink;

  g_return_if_fail (GST_IS_CMIO_VIDEO_SINK (object));

  cmio_sink = GST_CMIO_VIDEO_SINK (object);

  switch (prop_id) {
    case PROP_NAME:
    {
      cmio_sink->name = g_value_get_string (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cmio_video_sink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cmio_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCMIOVideoSink *cmio_sink;

  g_return_if_fail (GST_IS_CMIO_VIDEO_SINK (object));

  cmio_sink = GST_CMIO_VIDEO_SINK (object);

  switch (prop_id) {
    case PROP_NAME:
      g_value_take_string (value, cmio_sink->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_cmio_video_sink_start (GstBaseSink * bsink)
{
  return TRUE;
}

static gboolean
gst_cmio_video_sink_stop (GstBaseSink * bsink)
{
  return TRUE;
}

static void
gst_cmio_video_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstCMIOVideoSink *av_sink;

  av_sink = GST_CMIO_VIDEO_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else {
      if (GST_VIDEO_INFO_FPS_N (&av_sink->info) > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND,
            GST_VIDEO_INFO_FPS_D (&av_sink->info),
            GST_VIDEO_INFO_FPS_N (&av_sink->info));
      }
    }
  }
}

static GstCaps *
gst_cmio_video_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstCMIOVideoSink *av_sink = GST_CMIO_VIDEO_SINK (bsink);
  CFArrayRef formats;
  GstCaps *ret, *tmp;
  int i, n;

  formats =
      CVPixelFormatDescriptionArrayCreateWithAllPixelFormatTypes
      (kCFAllocatorDefault);

  ret = gst_caps_new_empty ();

  n = CFArrayGetCount (formats);
  for (i = 0; i < n; i++) {
    CFDictionaryRef attrs;
    CFNumberRef fourcc;
    unsigned int pixel_format;
    GstVideoFormat v_format;
    const char *format_str;
    char *caps_str;

    fourcc = (CFNumberRef)CFArrayGetValueAtIndex(formats, i);
    CFNumberGetValue (fourcc, kCFNumberIntType, &pixel_format);
    attrs = CVPixelFormatDescriptionCreateWithPixelFormatType (kCFAllocatorDefault,
        pixel_format);

    CFRelease (fourcc);

    v_format = _pixel_format_description_to_video_format (attrs);
    if (v_format != GST_VIDEO_FORMAT_UNKNOWN) {
      format_str = gst_video_format_to_string (v_format);

      caps_str = g_strdup_printf ("video/x-raw, format=%s", format_str);

      ret = gst_caps_merge (ret, gst_caps_from_string (caps_str));

      g_free (caps_str);
    }

    CFRelease (attrs);
  }

  ret = gst_caps_simplify (ret);

  gst_caps_set_simple (ret, "width", GST_TYPE_INT_RANGE, 0, G_MAXINT, "height",
      GST_TYPE_INT_RANGE, 0, G_MAXINT, "framerate", GST_TYPE_FRACTION_RANGE, 0,
      1, G_MAXINT, 1, NULL);
  GST_DEBUG_OBJECT (av_sink, "returning caps %" GST_PTR_FORMAT, ret);

  if (filter) {
    tmp = gst_caps_intersect_full (ret, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  CFRelease (formats);

  return ret;
}

static gboolean
gst_cmio_video_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstCMIOVideoSink *av_sink;
  gint width;
  gint height;
  gboolean ok;
  gint par_n, par_d;
  gint display_par_n, display_par_d;
  guint display_ratio_num, display_ratio_den;
  GstVideoInfo vinfo;
  GstStructure *structure;
  GstBufferPool *newpool, *oldpool;

  GST_DEBUG_OBJECT (bsink, "set caps with %" GST_PTR_FORMAT, caps);

  av_sink = GST_CMIO_VIDEO_SINK (bsink);

  ok = gst_video_info_from_caps (&vinfo, caps);
  if (!ok)
    return FALSE;

  width = GST_VIDEO_INFO_WIDTH (&vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&vinfo);

  par_n = GST_VIDEO_INFO_PAR_N (&vinfo);
  par_d = GST_VIDEO_INFO_PAR_D (&vinfo);

  if (!par_n)
    par_n = 1;

  display_par_n = 1;
  display_par_d = 1;

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  GST_TRACE_OBJECT (bsink, "PAR: %u/%u DAR:%u/%u", par_n, par_d, display_par_n,
      display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG_OBJECT (bsink, "keeping video height");
    GST_VIDEO_SINK_WIDTH (av_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (av_sink) = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG_OBJECT (bsink, "keeping video width");
    GST_VIDEO_SINK_WIDTH (av_sink) = width;
    GST_VIDEO_SINK_HEIGHT (av_sink) = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG_OBJECT (bsink, "approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (av_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (av_sink) = height;
  }
  GST_DEBUG_OBJECT (bsink, "scaling to %dx%d", GST_VIDEO_SINK_WIDTH (av_sink),
      GST_VIDEO_SINK_HEIGHT (av_sink));

  av_sink->info = vinfo;

  newpool = gst_video_buffer_pool_new ();
  structure = gst_buffer_pool_get_config (newpool);
  gst_buffer_pool_config_set_params (structure, caps, vinfo.size, 2, 0);
  gst_buffer_pool_set_config (newpool, structure);

  oldpool = av_sink->pool;
  /* we don't activate the pool yet, this will be done by downstream after it
   * has configured the pool. If downstream does not want our pool we will
   * activate it when we render into it */
  av_sink->pool = newpool;

  /* unref the old sink */
  if (oldpool) {
    /* we don't deactivate, some elements might still be using it, it will
     * be deactivated when the last ref is gone */
    gst_object_unref (oldpool);
  }

  return TRUE;
}

static GstFlowReturn
gst_cmio_video_sink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstCMIOVideoSink *av_sink;

  av_sink = GST_CMIO_VIDEO_SINK (bsink);

  GST_LOG_OBJECT (bsink, "preparing buffer:%p", buf);

  if (GST_VIDEO_SINK_WIDTH (av_sink) < 1 ||
      GST_VIDEO_SINK_HEIGHT (av_sink) < 1) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_cmio_video_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstCMIOVideoSink *av_sink;
  GstFlowReturn ret;

  GST_TRACE_OBJECT (vsink, "rendering buffer:%p", buf);

  av_sink = GST_CMIO_VIDEO_SINK (vsink);

  g_mutex_lock (&av_sink->render_lock);
  if (av_sink->buffer)
    gst_buffer_unref (av_sink->buffer);
  av_sink->buffer = gst_buffer_ref (buf);
  ret = av_sink->render_flow_return;

  if (!av_sink->layer_requesting_data)
    _request_data (av_sink);
  g_mutex_unlock (&av_sink->render_lock);

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && \
    MAC_OS_X_VERSION_MAX_ALLOWED >= 1010 && \
    defined(MAC_OS_X_VERSION_MIN_REQUIRED) && \
    MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_4
    CMIOBufferDisplayLayer *layer = GST_CMIO_VIDEO_SINK_LAYER(av_sink);
  if ([layer status] == AVQueuedSampleBufferRenderingStatusFailed) {
    GST_ERROR_OBJECT (av_sink, "failed to enqueue buffer on layer, %s",
        [[[layer error] description] UTF8String]);
    return GST_FLOW_ERROR;
  }
#endif

  return ret;
}

static gboolean
gst_cmio_video_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstCMIOVideoSink *av_sink = GST_CMIO_VIDEO_SINK (bsink);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  /* FIXME re-using buffer pool breaks renegotiation */
  if ((pool = av_sink->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (av_sink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (av_sink, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  } else {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    /* the normal size of a frame */
    size = info.size;
  }

  if (pool == NULL && need_pool) {
    GST_DEBUG_OBJECT (av_sink, "create new pool");
    pool = gst_video_buffer_pool_new ();

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  if (pool)
    gst_object_unref (pool);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (bsink, "failed setting config");
    return FALSE;
  }
}
