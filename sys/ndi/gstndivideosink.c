#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndivideosink.h"
#include "gstndiutil.h"

GST_DEBUG_CATEGORY_STATIC(gst_ndi_video_sink_debug);
#define GST_CAT_DEFAULT gst_ndi_video_sink_debug

#define gst_ndi_video_sink_parent_class parent_class
G_DEFINE_TYPE(GstNdiVideoSink, gst_ndi_video_sink, GST_TYPE_VIDEO_SINK);

enum
{
    PROP_0,
    PROP_DEVICE_PATH,
    PROP_DEVICE_NAME,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(NDI_VIDEO_TEMPLATE_CAPS));


static void gst_ndi_video_sink_finalize(GObject* object);
static void gst_ndi_video_sink_get_property(GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec);
static void gst_ndi_video_sink_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec);
static GstCaps* gst_ndi_video_sink_get_caps(GstBaseSink* basesink,
    GstCaps* filter);
static gboolean gst_ndi_video_sink_set_caps(GstBaseSink* basesink, GstCaps* caps);
static gboolean gst_ndi_video_sink_start(GstBaseSink* basesink);
static gboolean gst_ndi_video_sink_stop(GstBaseSink* basesink);
static GstFlowReturn gst_ndi_video_sink_prepare(GstBaseSink* basesink,
    GstBuffer* buf);
static GstFlowReturn gst_ndi_video_sink_show_frame(GstVideoSink* vsink,
    GstBuffer* buffer);


static void
gst_ndi_video_sink_class_init(GstNdiVideoSinkClass* klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass* gstbasesink_class = (GstBaseSinkClass*)klass;
	GstVideoSinkClass* video_sink_class = GST_VIDEO_SINK_CLASS(klass);

    gobject_class->finalize = gst_ndi_video_sink_finalize;
    gobject_class->get_property = gst_ndi_video_sink_get_property;
    gobject_class->set_property = gst_ndi_video_sink_set_property;

    gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR(gst_ndi_video_sink_get_caps);
    gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_ndi_video_sink_set_caps);
    gstbasesink_class->start = GST_DEBUG_FUNCPTR(gst_ndi_video_sink_start);
    gstbasesink_class->stop = GST_DEBUG_FUNCPTR(gst_ndi_video_sink_stop);
    //gstbasesink_class->prepare = GST_DEBUG_FUNCPTR(gst_ndi_video_sink_prepare);

    video_sink_class->show_frame =
        GST_DEBUG_FUNCPTR(gst_ndi_video_sink_show_frame);

    g_object_class_install_property(gobject_class, PROP_DEVICE_PATH,
        g_param_spec_string("device-path", "Device Path",
            "The device path", "",
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_DEVICE_NAME,
        g_param_spec_string("device-name", "Device Name",
            "The human-readable device name", "",
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(element_class,
        "NDI Video Source",
        "Source/Video/Hardware",
        "Capture video stream from NDI device",
        "support@teaminua.com");

    gst_element_class_add_static_pad_template(element_class, &sink_template);

    GST_DEBUG_CATEGORY_INIT(gst_ndi_video_sink_debug, "ndivideosink", 0,
        "ndivideosink");
}

static void
gst_ndi_video_sink_init(GstNdiVideoSink* self)
{
}

static void
gst_ndi_video_sink_finalize(GObject* object)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(object);

    GST_DEBUG_OBJECT(self, "Finalize");
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_ndi_video_sink_get_property(GObject* object, guint prop_id, GValue* value,
    GParamSpec* pspec)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, self->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_value_set_string(value, self->device_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_ndi_video_sink_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_free(self->device_path);
        self->device_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "%s", self->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_free(self->device_name);
        self->device_name = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "%s", self->device_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstCaps* gst_ndi_video_sink_get_caps(GstBaseSink* basesink,
    GstCaps* filter) {
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);

    GstCaps* caps = NULL;

    caps = gst_pad_get_pad_template_caps(GST_VIDEO_SINK_PAD(self));

    GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, caps);

    return caps;
}

static NDIlib_send_instance_t pNDI_send;
static NDIlib_video_frame_v2_t NDI_video_frame;
static gboolean gst_ndi_video_sink_set_caps(GstBaseSink* basesink, GstCaps* caps) {
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, caps);
    
    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        return FALSE;
    }

    switch (videoInfo.finfo->format) {
    case GST_VIDEO_FORMAT_UYVY:
        NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;
        break;
    case GST_VIDEO_FORMAT_I420:
        NDI_video_frame.FourCC = NDIlib_FourCC_video_type_I420;
        break;
    case GST_VIDEO_FORMAT_NV12:
        NDI_video_frame.FourCC = NDIlib_FourCC_video_type_NV12;
        break;
    case GST_VIDEO_FORMAT_BGRA:
        NDI_video_frame.FourCC = NDIlib_FourCC_video_type_BGRA;
        break;
    case GST_VIDEO_FORMAT_RGBA:
        NDI_video_frame.FourCC = NDIlib_FourCC_video_type_RGBA;
        break;
    }

    switch (videoInfo.interlace_mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
        NDI_video_frame.frame_format_type = NDIlib_frame_format_type_progressive;
        break;
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
        NDI_video_frame.frame_format_type = NDIlib_frame_format_type_interleaved;
        break;
    }

    NDI_video_frame.xres = videoInfo.width;
    NDI_video_frame.yres = videoInfo.height;
    NDI_video_frame.frame_rate_N = videoInfo.fps_n;
    NDI_video_frame.frame_rate_D = videoInfo.fps_d;
    
    NDI_video_frame.p_data = (uint8_t*)malloc(videoInfo.size);

    return TRUE;
}

static gboolean gst_ndi_video_sink_start(GstBaseSink* basesink) {
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "Start %s", self->device_name);

    NDIlib_send_create_t NDI_send_create_desc;
    NDI_send_create_desc.p_ndi_name = self->device_name;
    NDI_send_create_desc.p_groups = NULL;
    NDI_send_create_desc.clock_audio = TRUE;
    NDI_send_create_desc.clock_video = TRUE;

    pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
    if (!pNDI_send) {
        GST_DEBUG_OBJECT(self, "Failed");
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_ndi_video_sink_stop(GstBaseSink* basesink) {
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "Stop");

    free((void*)NDI_video_frame.p_data);
    NDIlib_send_destroy(pNDI_send);

    return TRUE;
}

static GstFlowReturn gst_ndi_video_sink_prepare(GstBaseSink* basesink,
    GstBuffer* buf) {
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "Prepare");

    return GST_FLOW_OK;
}

static GstFlowReturn gst_ndi_video_sink_show_frame(GstVideoSink* vsink,
    GstBuffer* buffer) {
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(vsink);
    GST_DEBUG_OBJECT(self, ">");

    /*GstVideoFrame videoFrame;
    GstVideoInfo videoInfo;
    if (!gst_video_frame_map(&videoFrame, &videoInfo, buffer, GST_MAP_READ)) {
        return GST_FLOW_ERROR;
    }
    
    gst_video_frame_unmap(&videoFrame);*/

    auto bufferSize = gst_buffer_get_size(buffer);
    bufferSize = gst_buffer_extract(buffer, 0, NDI_video_frame.p_data, bufferSize);

    NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);
    
    return GST_FLOW_OK;
}
