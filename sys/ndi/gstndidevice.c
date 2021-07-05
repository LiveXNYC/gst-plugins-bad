#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndidevice.h"
#include <Processing.NDI.Lib.h>

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

enum
{
    PROP_0,
    PROP_DEVICE_PATH,
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
        g_param_spec_string("device", "Device string ID",
            "Device strId", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS)));
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

    G_OBJECT_CLASS(gst_ndi_device_parent_class)->finalize(object);
    
    NDIlib_destroy();
}

static GstElement*
gst_ndi_device_create_element(GstDevice* device, const gchar* name)
{
    GstNdiDevice* self = GST_NDI_DEVICE(device);
    GstElement* elem = NULL;

    if (self->isVideo) {
        elem = gst_element_factory_make("ndivideosrc", name);
    }
    else {
        elem = gst_element_factory_make("ndiaudiosrc", name);
    }

    if (elem) {
        g_object_set(elem, "device-path", self->device_path, NULL);
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
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}
