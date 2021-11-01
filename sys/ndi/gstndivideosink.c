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

    video_sink_class->show_frame =
        GST_DEBUG_FUNCPTR(gst_ndi_video_sink_show_frame);

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
    self->device_name = NULL;
}

static void
gst_ndi_video_sink_finalize(GObject* object)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(object);
    GST_DEBUG_OBJECT(self, "Finalize");

    if (self->device_name) {
        g_free(self->device_name);
        self->device_name = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_ndi_video_sink_get_property(GObject* object, guint prop_id, GValue* value,
    GParamSpec* pspec)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(object);

    switch (prop_id) {
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

static GstCaps* 
gst_ndi_video_sink_get_caps(GstBaseSink* basesink, GstCaps* filter)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);

    GstCaps* caps = NULL;

    caps = gst_pad_get_pad_template_caps(GST_VIDEO_SINK_PAD(self));

    GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, caps);

    return caps;
}

static gboolean 
gst_ndi_video_sink_set_caps(GstBaseSink* basesink, GstCaps* caps)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, caps);
    
    return gst_ndi_output_create_video_frame(self->output, caps);
}

static gboolean 
gst_ndi_video_sink_start(GstBaseSink* basesink)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "Start %s", self->device_name);

    self->output = gst_ndi_output_acquire(self->device_name, GST_ELEMENT(self), TRUE);

    return self->output != NULL;
}

static gboolean 
gst_ndi_video_sink_stop(GstBaseSink* basesink)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(basesink);
    GST_DEBUG_OBJECT(self, "Stop");

    gst_ndi_output_release(self->device_name, GST_ELEMENT(self), TRUE);

    return TRUE;
}

static GstFlowReturn 
gst_ndi_video_sink_show_frame(GstVideoSink* vsink, GstBuffer* buffer)
{
    GstNdiVideoSink* self = GST_NDI_VIDEO_SINK(vsink);

    gboolean res = gst_ndi_output_send_video_buffer(self->output, buffer);
    
    return res ? GST_FLOW_OK : GST_FLOW_ERROR;
}
