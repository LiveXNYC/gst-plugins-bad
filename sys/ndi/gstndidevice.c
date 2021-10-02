#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndidevice.h"
#include "gstndiutil.h"
#include <ndi/Processing.NDI.Lib.h>

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

enum
{
    PROP_0,
    PROP_DEVICE_PATH,
    PROP_DEVICE_NAME,
};

G_DEFINE_TYPE(GstNdiDevice, gst_ndi_device, GST_TYPE_DEVICE);


static void gst_ndi_device_get_property(GObject* object,
    guint prop_id, GValue* value, GParamSpec* pspec);
static void gst_ndi_device_set_property(GObject* object,
    guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_ndi_device_finalize(GObject* object);
static GstElement* gst_ndi_device_create_element(GstDevice* device,
    const gchar* name);

static void
gst_ndi_device_class_init(GstNdiDeviceClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstDeviceClass* dev_class = GST_DEVICE_CLASS(klass);

    dev_class->create_element = gst_ndi_device_create_element;

    gobject_class->get_property = gst_ndi_device_get_property;
    gobject_class->set_property = gst_ndi_device_set_property;
    gobject_class->finalize = gst_ndi_device_finalize;

    g_object_class_install_property(gobject_class, PROP_DEVICE_PATH,
        g_param_spec_string("device-path", "Device string ID",
            "Device strId", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_DEVICE_NAME,
        g_param_spec_string("device-name", "Device Name",
            "The human-readable device name", "",
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));
}

static void
gst_ndi_device_init(GstNdiDevice* self)
{

}

static void
gst_ndi_device_finalize(GObject* object)
{
    GstNdiDevice* self = GST_NDI_DEVICE(object);

    g_free(self->device_path);
    g_free(self->device_name);

    G_OBJECT_CLASS(gst_ndi_device_parent_class)->finalize(object);
}

static GstElement*
gst_ndi_device_create_element(GstDevice* device, const gchar* name)
{
    GstNdiDevice* self = GST_NDI_DEVICE(device);

    GstElement* elem = gst_element_factory_make(self->isVideo ? "ndivideosrc" : "ndiaudiosrc", name);
    if (elem) {
        g_object_set(elem, "device-path", self->device_path, NULL);
        g_object_set(elem, "device-name", self->device_name, NULL);
    }

    return elem;
}

static void
gst_ndi_device_get_property(GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    GstNdiDevice* self = GST_NDI_DEVICE(object);

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
gst_ndi_device_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstNdiDevice* self = GST_NDI_DEVICE(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        self->device_path = g_value_dup_string(value);
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

static GstDevice*
gst_ndi_device_provider_create_src_device(const char* id, const char* name, gboolean isVideo) {
    GstStructure* props = gst_structure_new("ndi-proplist",
        "device.api", G_TYPE_STRING, "NDI",
        "device.strid", G_TYPE_STRING, GST_STR_NULL(id),
        "device.friendlyName", G_TYPE_STRING, name, NULL);

    GstCaps* caps = isVideo
        ? gst_util_create_default_video_caps()
        : gst_util_create_default_audio_caps();

    GstDevice* device = g_object_new(GST_TYPE_NDI_DEVICE, "device-path", id, 
        "device-name", name,
        "display-name", name,
        "caps", caps,
        "device-class", isVideo ? "Video/Source" : "Audio/Source",
        "properties", props,
        NULL);
    GST_NDI_DEVICE(device)->isVideo = isVideo;
    if (caps) {
        gst_caps_unref(caps);
    }
    gst_structure_free(props);

    return device;
}

GstDevice*
gst_ndi_device_provider_create_video_src_device(const char* id, const char* name) {
    return gst_ndi_device_provider_create_src_device(id, name, TRUE);
}

GstDevice*
gst_ndi_device_provider_create_audio_src_device(const char* id, const char* name) {
    return gst_ndi_device_provider_create_src_device(id, name, FALSE);
}
