#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndivideosrc.h"
#include "gstndiutil.h"
#include <string.h>

GST_DEBUG_CATEGORY (gst_ndi_video_src_debug);
#define GST_CAT_DEFAULT gst_ndi_video_src_debug

enum
{
  PROP_0,
  PROP_DEVICE_PATH,
  PROP_DEVICE_NAME,
};

#define GST_MF_VIDEO_FORMATS \
  "{ UYVY, UYVA, BGRA, RGBA }"

#define GST_NDI_VIDEO_CAPS_MAKE(format)                                     \
"video/x-raw"                                                     
#define SRC_TEMPLATE_CAPS GST_NDI_VIDEO_CAPS_MAKE (GST_MF_VIDEO_FORMATS)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(SRC_TEMPLATE_CAPS));


static void gst_ndi_video_src_finalize (GObject * object);
static void gst_ndi_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ndi_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_ndi_video_src_start (GstBaseSrc * src);
static gboolean gst_ndi_video_src_stop (GstBaseSrc * src);
static gboolean gst_ndi_video_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_ndi_video_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static GstCaps *gst_ndi_video_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_ndi_video_src_unlock (GstBaseSrc * src);
static gboolean gst_ndi_video_src_unlock_stop (GstBaseSrc * src);

static GstFlowReturn
gst_ndi_video_src_fill(GstPushSrc* pushsrc, GstBuffer* buf);

#define gst_ndi_video_src_parent_class parent_class
G_DEFINE_TYPE (GstNdiVideoSrc, gst_ndi_video_src, GST_TYPE_PUSH_SRC);

static void
gst_ndi_video_src_class_init (GstNdiVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = gst_ndi_video_src_finalize;
  gobject_class->get_property = gst_ndi_video_src_get_property;
  gobject_class->set_property = gst_ndi_video_src_set_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE_PATH,
      g_param_spec_string ("device-path", "Device Path",
          "The device path", "",
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device Name",
          "The human-readable device name", "",
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "NDI Video Source",
      "Source/Video/Hardware",
      "Capture video stream from NDI device",
      "support@teaminua.com");

  gst_element_class_add_static_pad_template (element_class, &src_template);

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_ndi_video_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_ndi_video_src_stop);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_ndi_video_src_set_caps);
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_ndi_video_src_get_caps);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR (gst_ndi_video_src_fixate);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_ndi_video_src_unlock);
  basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_ndi_video_src_unlock_stop);

  pushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_ndi_video_src_fill);

  GST_DEBUG_CATEGORY_INIT (gst_ndi_video_src_debug, "ndivideosrc", 0,
      "ndivideosrc");
}

static void
gst_ndi_video_src_init (GstNdiVideoSrc * self)
{
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), TRUE);

  self->pNDI_recv = NULL;
  self->xres = 0;
  self->yres = 0;
  self->frame_rate_N = 0;
  self->frame_rate_D = 0;
  self->device_path = NULL;
  self->device_name = NULL;
}

static void
gst_ndi_video_src_finalize (GObject * object)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(object);

  if (self->device_name) {
      g_free(self->device_name);
      self->device_name = NULL;
  }
  if (self->device_path) {
      g_free (self->device_path);
      self->device_path = NULL;
  }
  if (self->pNDI_recv != NULL) {
      NDIlib_recv_destroy(self->pNDI_recv);
      self->pNDI_recv = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ndi_video_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(object);

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      g_value_set_string (value, self->device_path);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ndi_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(object);
  
  switch (prop_id) {
    case PROP_DEVICE_PATH:
      g_free (self->device_path);
      self->device_path = g_value_dup_string (value);
      GST_DEBUG_OBJECT(self, "%s", self->device_path);
      break;
    case PROP_DEVICE_NAME:
      g_free (self->device_name);
      self->device_name = g_value_dup_string (value);
      GST_DEBUG_OBJECT(self, "%s", self->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ndi_video_src_send_caps_event(GstBaseSrc* src, const NDIlib_video_frame_v2_t* frame) {
    GstPad* srcpad = GST_BASE_SRC_PAD(src);
    GstCaps* caps = gst_util_create_video_caps(frame);
    GstEvent* event = gst_pad_get_sticky_event(srcpad, GST_EVENT_CAPS, 0);
    if (event) {
        GstCaps* event_caps;
        gst_event_parse_caps(event, &event_caps);
        if (caps != event_caps) {
            gst_event_unref(event);
            event = gst_event_new_caps(caps);
        }
    }
    else {
        event = gst_event_new_caps(caps);
    }
    gst_pad_push_event(srcpad, event);
    gst_caps_unref(caps);
}

static gboolean
gst_ndi_video_src_start (GstBaseSrc * src)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(src);

  NDIlib_recv_create_v3_t create;
  create.source_to_connect_to.p_url_address = self->device_path;
  create.source_to_connect_to.p_ndi_name = "";
  create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
  create.bandwidth = NDIlib_recv_bandwidth_highest;
  create.allow_video_fields = FALSE;
  create.p_ndi_recv_name = NULL;

  self->pNDI_recv = NDIlib_recv_create_v3(&create);


  if (self->pNDI_recv != NULL) {
      NDIlib_video_frame_v2_t video_frame = gst_ndi_util_get_video_frame(self->pNDI_recv, 5000);
      if (video_frame.xres > 0 && video_frame.yres > 0) {
          gst_ndi_video_src_send_caps_event(src, &video_frame);
          self->xres = video_frame.xres;
          self->yres = video_frame.yres;
          self->frame_rate_N = video_frame.frame_rate_N;
          self->frame_rate_D = video_frame.frame_rate_D;
          NDIlib_recv_free_video_v2(self->pNDI_recv, &video_frame);
      }
  }

  return TRUE;
}

static gboolean
gst_ndi_video_src_stop (GstBaseSrc * src)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(src);

  if (self->pNDI_recv != NULL) {
      NDIlib_recv_destroy(self->pNDI_recv);
      self->pNDI_recv = NULL;
  }

  return TRUE;
}

static gboolean
gst_ndi_video_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(src);

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  return TRUE;
}

static GstCaps *
gst_ndi_video_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(src);
  GstCaps *caps = NULL;

  if (self->pNDI_recv != NULL) {
      NDIlib_video_frame_v2_t video_frame = gst_ndi_util_get_video_frame(self->pNDI_recv, 5000);
      if (video_frame.xres > 0 && video_frame.yres > 0) {
          caps = gst_util_create_video_caps(&video_frame);
          NDIlib_recv_free_video_v2(self->pNDI_recv, &video_frame);
          GST_DEBUG_OBJECT(self, "Video frame caps %" GST_PTR_FORMAT, caps);
      }
  }

  if (!caps)
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));

  if (filter) {
    GstCaps *filtered =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = filtered;
  }

  return caps;
}

static GstCaps *
gst_ndi_video_src_fixate (GstBaseSrc * src, GstCaps * caps) {
  GstStructure *structure;
  GstCaps *fixated_caps;
  guint i;

  GST_DEBUG("fixate caps %" GST_PTR_FORMAT, caps);

  fixated_caps = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (fixated_caps); ++i) {
    structure = gst_caps_get_structure (fixated_caps, i);
    gst_structure_fixate_field_nearest_int (structure, "width", G_MAXINT);
    gst_structure_fixate_field_nearest_int (structure, "height", G_MAXINT);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        G_MAXINT, 1);
  }

  fixated_caps = gst_caps_fixate (fixated_caps);

  return fixated_caps;
}

static gboolean
gst_ndi_video_src_unlock (GstBaseSrc * src) {
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(src);

  return TRUE;
}

static gboolean
gst_ndi_video_src_unlock_stop (GstBaseSrc * src) {
  GstNdiVideoSrc *self = GST_NDI_VIDEO_SRC(src);

  return TRUE;
}

static GstFlowReturn
gst_ndi_video_src_fill(GstPushSrc* pushsrc, GstBuffer* buf) {
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(pushsrc);

    if (self->pNDI_recv != NULL) {
        NDIlib_video_frame_v2_t video_frame = gst_ndi_util_get_video_frame(self->pNDI_recv, 100);
        if (video_frame.xres > 0 && video_frame.yres > 0) {
            GstMapInfo info;
            if (!gst_buffer_map(buf, &info, GST_MAP_WRITE))
            {
                NDIlib_recv_free_video_v2(self->pNDI_recv, &video_frame);
                return GST_FLOW_ERROR;
            }
            guint8* data = info.data;
            memcpy(data, video_frame.p_data, info.size);
            GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;
            gst_buffer_unmap(buf, &info);

            NDIlib_recv_free_video_v2(self->pNDI_recv, &video_frame);
        }
        else {
        }
    }

    return GST_FLOW_OK;
}
