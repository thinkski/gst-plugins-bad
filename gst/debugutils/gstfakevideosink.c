/*
 * GStreamer
 * Copyright (C) 2017 Collabora Inc.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
 * SECTION:element-fakevideosink
 * @title: fakevideosink
 *
 * This element is the same as fakesink but will pretend to support various
 * allocation meta API like GstVideoMeta in order to prevent memory copies.
 * This is useful for throughput testing and testing zero-copy path while
 * creating a new pipeline.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 videotestsrc ! fakevideosink
 * gst-launch-1.0 videotestsrc ! fpsdisplaysink text-overlay=false video-sink=fakevideosink
 * ]|
 *
 * Since 1.14
 */

#include "gstfakevideosink.h"

#include <gst/video/video.h>

#define C_FLAGS(v) ((guint) v)

GType
gst_fake_video_sink_allocation_meta_flags_get_type (void)
{
  static const GFlagsValue values[] = {
    {C_FLAGS (GST_ALLOCATION_FLAG_CROP_META),
        "Expose the crop meta as supported", "crop"},
    {C_FLAGS (GST_ALLOCATION_FLAG_OVERLAY_COMPOSITION_META),
          "Expose the overlay composition meta as supported",
        "overlay-composition"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id =
        g_flags_register_static ("GstFakeVideoSinkAllocationMetaFlags", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

enum
{
  PROP_0,
  PROP_ALLOCATION_META_FLAGS,
  PROP_LAST
};

#define ALLOCATION_META_DEFAULT_FLAGS GST_ALLOCATION_FLAG_CROP_META | GST_ALLOCATION_FLAG_OVERLAY_COMPOSITION_META

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY",
            GST_VIDEO_FORMATS_ALL)));

G_DEFINE_TYPE (GstFakeVideoSink, gst_fake_video_sink, GST_TYPE_BIN);

static gboolean
gst_fake_video_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFakeVideoSink *self = GST_FAKE_VIDEO_SINK (parent);
  GstCaps *caps;
  GstVideoInfo info;
  guint min_buffers = 1;

  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return gst_pad_query_default (pad, parent, query);

  gst_query_parse_allocation (query, &caps, NULL);
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  /* Request an extra buffer if we are keeping a ref on the last rendered buffer */
  if (gst_base_sink_is_last_sample_enabled (GST_BASE_SINK (self->child)))
    min_buffers++;

  gst_query_add_allocation_pool (query, NULL, info.size, min_buffers, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  GST_OBJECT_LOCK (self);
  if (self->allocation_meta_flags & GST_ALLOCATION_FLAG_CROP_META)
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  if (self->allocation_meta_flags &
      GST_ALLOCATION_FLAG_OVERLAY_COMPOSITION_META)
    gst_query_add_allocation_meta (query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);

  GST_OBJECT_UNLOCK (self);

  /* add here any meta API that would help support zero-copy */

  return TRUE;
}

/* TODO complete the types and make this an utility */
static void
gst_fake_video_sink_proxy_properties (GstFakeVideoSink * self,
    GstElement * child)
{
  static gsize initialized = 0;

  if (g_once_init_enter (&initialized)) {
    GObjectClass *object_class;
    GParamSpec **properties;
    guint n_properties, i;

    object_class = G_OBJECT_CLASS (GST_FAKE_VIDEO_SINK_GET_CLASS (self));
    properties = g_object_class_list_properties (G_OBJECT_GET_CLASS (child),
        &n_properties);

    /**
     * GstFakeVideoSink:allocation-meta-flags
     *
     * Control the behaviour of the sink allocation query handler.
     *
     * Since: 1.18
     */
    g_object_class_install_property (object_class, PROP_ALLOCATION_META_FLAGS,
        g_param_spec_flags ("allocation-meta-flags", "Flags",
            "Flags to control behaviour",
            GST_TYPE_FAKE_VIDEO_SINK_ALLOCATION_META_FLAGS,
            ALLOCATION_META_DEFAULT_FLAGS,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


    for (i = 0; i < n_properties; i++) {
      guint property_id = i + PROP_LAST;

      if (properties[i]->owner_type != G_OBJECT_TYPE (child) &&
          properties[i]->owner_type != GST_TYPE_BASE_SINK)
        continue;

      if (G_IS_PARAM_SPEC_BOOLEAN (properties[i])) {
        GParamSpecBoolean *prop = G_PARAM_SPEC_BOOLEAN (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_boolean (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                prop->default_value, properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_INT (properties[i])) {
        GParamSpecInt *prop = G_PARAM_SPEC_INT (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_int (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                prop->minimum, prop->maximum, prop->default_value,
                properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_UINT (properties[i])) {
        GParamSpecUInt *prop = G_PARAM_SPEC_UINT (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_uint (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                prop->minimum, prop->maximum, prop->default_value,
                properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_INT64 (properties[i])) {
        GParamSpecInt64 *prop = G_PARAM_SPEC_INT64 (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_int64 (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                prop->minimum, prop->maximum, prop->default_value,
                properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_UINT64 (properties[i])) {
        GParamSpecUInt64 *prop = G_PARAM_SPEC_UINT64 (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_uint64 (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                prop->minimum, prop->maximum, prop->default_value,
                properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_ENUM (properties[i])) {
        GParamSpecEnum *prop = G_PARAM_SPEC_ENUM (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_enum (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                properties[i]->value_type, prop->default_value,
                properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_STRING (properties[i])) {
        GParamSpecString *prop = G_PARAM_SPEC_STRING (properties[i]);
        g_object_class_install_property (object_class, property_id,
            g_param_spec_string (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                prop->default_value, properties[i]->flags));
      } else if (G_IS_PARAM_SPEC_BOXED (properties[i])) {
        g_object_class_install_property (object_class, property_id,
            g_param_spec_boxed (g_param_spec_get_name (properties[i]),
                g_param_spec_get_nick (properties[i]),
                g_param_spec_get_blurb (properties[i]),
                properties[i]->value_type, properties[i]->flags));
      }
    }

    g_free (properties);
    g_once_init_leave (&initialized, 1);
  }
}

static void
gst_fake_video_sink_init (GstFakeVideoSink * self)
{
  GstElement *child;
  GstPadTemplate *template = gst_static_pad_template_get (&sink_factory);

  child = gst_element_factory_make ("fakesink", "sink");

  self->allocation_meta_flags = ALLOCATION_META_DEFAULT_FLAGS;

  if (child) {
    GstPad *sink_pad = gst_element_get_static_pad (child, "sink");
    GstPad *ghost_pad;

    /* mimic GstVideoSink base class */
    g_object_set (child, "max-lateness", 5 * GST_MSECOND,
        "processing-deadline", 15 * GST_MSECOND, "qos", TRUE, "sync", TRUE,
        NULL);

    gst_bin_add (GST_BIN (self), child);

    ghost_pad = gst_ghost_pad_new_from_template ("sink", sink_pad, template);
    gst_object_unref (template);
    gst_element_add_pad (GST_ELEMENT (self), ghost_pad);
    gst_object_unref (sink_pad);

    gst_pad_set_query_function (ghost_pad, gst_fake_video_sink_query);

    self->child = child;

    gst_fake_video_sink_proxy_properties (self, child);
  } else {
    g_warning ("Check your GStreamer installation, "
        "core element 'fakesink' is missing.");
  }
}

static void
gst_fake_video_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstFakeVideoSink *self = GST_FAKE_VIDEO_SINK (object);

  switch (property_id) {
    case PROP_ALLOCATION_META_FLAGS:
      GST_OBJECT_LOCK (self);
      g_value_set_flags (value, self->allocation_meta_flags);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      g_object_get_property (G_OBJECT (self->child), pspec->name, value);
      break;
  }
}

static void
gst_fake_video_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFakeVideoSink *self = GST_FAKE_VIDEO_SINK (object);

  switch (property_id) {
    case PROP_ALLOCATION_META_FLAGS:
      GST_OBJECT_LOCK (self);
      self->allocation_meta_flags = g_value_get_flags (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      g_object_set_property (G_OBJECT (self->child), pspec->name, value);
      break;
  }
}

static void
gst_fake_video_sink_class_init (GstFakeVideoSinkClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gst_fake_video_sink_get_property;
  object_class->set_property = gst_fake_video_sink_set_property;

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_static_metadata (element_class, "Fake Video Sink",
      "Video/Sink", "Fake video display that allows zero-copy",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com>");

  gst_type_mark_as_plugin_api (GST_TYPE_FAKE_VIDEO_SINK_ALLOCATION_META_FLAGS);
}
