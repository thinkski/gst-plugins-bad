/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <locale.h>

#include "vkwindow_xcb.h"
#include "vkdisplay_xcb.h"

#define GST_VULKAN_WINDOW_XCB_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_VULKAN_WINDOW_XCB, GstVulkanWindowXCBPrivate))

#define GST_CAT_DEFAULT gst_vulkan_window_xcb_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkanwindowxcb", 0,
        "Vulkan XCB Window");
    g_once_init_leave (&_init, 1);
  }
}

#define gst_vulkan_window_xcb_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanWindowXCB, gst_vulkan_window_xcb,
    GST_TYPE_VULKAN_WINDOW, _init_debug ());

gboolean gst_vulkan_window_xcb_handle_event (GstVulkanWindowXCB * window_xcb);

enum
{
  PROP_0,
};

struct _GstVulkanWindowXCBPrivate
{
  gboolean activate;
  gboolean activate_result;

  gint preferred_width;
  gint preferred_height;

  xcb_intern_atom_reply_t *atom_wm_delete_window;
};

static gpointer gst_vulkan_window_xcb_get_platform_window (GstVulkanWindow *
    window);
gboolean gst_vulkan_window_xcb_open (GstVulkanWindow * window, GError ** error);
void gst_vulkan_window_xcb_close (GstVulkanWindow * window);

static void
gst_vulkan_window_xcb_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_window_xcb_class_init (GstVulkanWindowXCBClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstVulkanWindowClass *window_class = (GstVulkanWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstVulkanWindowXCBPrivate));

  obj_class->finalize = gst_vulkan_window_xcb_finalize;

  window_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_window_xcb_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_window_xcb_close);
  window_class->get_platform_handle = gst_vulkan_window_xcb_get_platform_window;
}

static void
gst_vulkan_window_xcb_init (GstVulkanWindowXCB * window)
{
  window->priv = GST_VULKAN_WINDOW_XCB_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstVulkanWindowXCB *
gst_vulkan_window_xcb_new (GstVulkanDisplay * display)
{
  _init_debug ();

  if ((gst_vulkan_display_get_handle_type (display) &
          GST_VULKAN_DISPLAY_TYPE_XCB)
      == GST_VULKAN_DISPLAY_TYPE_NONE) {
    GST_INFO ("Wrong display type %u for this window type %u", display->type,
        GST_VULKAN_DISPLAY_TYPE_XCB);
    return NULL;
  }

  return g_object_new (GST_TYPE_VULKAN_WINDOW_XCB, NULL);
}

static void
gst_vulkan_window_xcb_show (GstVulkanWindow * window)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = GST_VULKAN_DISPLAY_XCB (window->display);
  xcb_connection_t *connection = display_xcb->platform_handle.connection;

  if (!window_xcb->visible) {
    xcb_map_window (connection, window_xcb->win_id);
    window_xcb->visible = TRUE;
  }
}

static void
gst_vulkan_window_xcb_hide (GstVulkanWindow * window)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = GST_VULKAN_DISPLAY_XCB (window->display);
  xcb_connection_t *connection = display_xcb->platform_handle.connection;

  if (window_xcb->visible) {
    xcb_unmap_window (connection, window_xcb->win_id);
    window_xcb->visible = FALSE;
  }
}

gboolean
gst_vulkan_window_xcb_create_window (GstVulkanWindowXCB * window_xcb)
{
  GstVulkanDisplayXCB *display_xcb;
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t root_window;
  uint32_t value_mask, value_list[32];
  xcb_intern_atom_cookie_t cookie, cookie2;
  xcb_intern_atom_reply_t *reply, *reply2;
//  const gchar *title = "OpenGL renderer";
  gint x = 0, y = 0, width = 320, height = 240;

  display_xcb =
      GST_VULKAN_DISPLAY_XCB (GST_VULKAN_WINDOW (window_xcb)->display);
  connection = GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);
  root_window = GST_VULKAN_DISPLAY_XCB_ROOT_WINDOW (display_xcb);
  screen = GST_VULKAN_DISPLAY_XCB_SCREEN (display_xcb);

  window_xcb->win_id = xcb_generate_id (connection);

  value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  value_list[0] = screen->black_pixel;
  value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE;

  xcb_create_window (connection, XCB_COPY_FROM_PARENT, window_xcb->win_id,
      root_window, x, y, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
      screen->root_visual, value_mask, value_list);

  GST_LOG_OBJECT (window_xcb, "gl window id: %p",
      (gpointer) (guintptr) window_xcb->win_id);
  GST_LOG_OBJECT (window_xcb, "gl window props: x:%d y:%d", x, y);

  /* Magic code that will send notification when window is destroyed */
  cookie = xcb_intern_atom (connection, 1, 12, "WM_PROTOCOLS");
  reply = xcb_intern_atom_reply (connection, cookie, 0);

  cookie2 = xcb_intern_atom (connection, 0, 16, "WM_DELETE_WINDOW");
  reply2 = xcb_intern_atom_reply (connection, cookie2, 0);

  xcb_change_property (connection, XCB_PROP_MODE_REPLACE, window_xcb->win_id,
      reply->atom, 4, 32, 1, &reply2->atom);
  g_free (reply);
  g_free (reply2);

  gst_vulkan_window_xcb_show (GST_VULKAN_WINDOW (window_xcb));

  return TRUE;
}

static gpointer
gst_vulkan_window_xcb_get_platform_window (GstVulkanWindow * window)
{
  return &GST_VULKAN_WINDOW_XCB (window)->win_id;
}

gboolean
gst_vulkan_window_xcb_open (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = (GstVulkanDisplayXCB *) window->display;
  xcb_connection_t *connection;

  connection = display_xcb->platform_handle.connection;
  if (connection == NULL) {
    g_set_error (error, GST_VULKAN_WINDOW_ERROR,
        GST_VULKAN_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to connect to X display server with XCB");
    goto failure;
  }

  if (!GST_VULKAN_WINDOW_CLASS (parent_class)->open (window, error))
    return FALSE;

  return gst_vulkan_window_xcb_create_window (window_xcb);

failure:
  return FALSE;
}

void
gst_vulkan_window_xcb_close (GstVulkanWindow * window)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = (GstVulkanDisplayXCB *) window->display;
  xcb_connection_t *connection =
      GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);

  if (connection) {
    gst_vulkan_window_xcb_hide (window);

    g_free (window_xcb->priv->atom_wm_delete_window);
    window_xcb->priv->atom_wm_delete_window = NULL;
    GST_DEBUG ("display receiver closed");
  }

  GST_VULKAN_WINDOW_CLASS (parent_class)->close (window);
}
