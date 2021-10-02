#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndideviceprovider.h"
#include "gstndidevice.h"
#include "gstndiutil.h"
#include "gstndifinder.h"

#define gst_ndi_device_provider_parent_class parent_class
G_DEFINE_TYPE(GstNdiDeviceProvider, gst_ndi_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

struct _GstNdiDeviceProviderPrivate {
    GstNdiFinder* finder;
    GList* devices;
};

static GList* 
gst_ndi_device_provider_probe(GstDeviceProvider* provider);
static gboolean
gst_ndi_device_provider_start(GstDeviceProvider* provider);
static void
gst_ndi_device_provider_stop(GstDeviceProvider* provider);
static void
gst_ndi_device_provider_device_changed(GstObject* provider, gboolean isAdd, gchar* id, gchar* name);
static void
gst_ndi_device_provider_finalize(GObject* object);
static void
gst_ndi_device_provider_dispose(GObject* object);

static void
gst_ndi_device_provider_class_init(GstNdiDeviceProviderClass* klass)
{
    GstDeviceProviderClass* provider_class = GST_DEVICE_PROVIDER_CLASS(klass);
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = gst_ndi_device_provider_dispose;
    gobject_class->finalize = gst_ndi_device_provider_finalize;

    provider_class->probe = GST_DEBUG_FUNCPTR(gst_ndi_device_provider_probe);
    provider_class->start = gst_ndi_device_provider_start;
    provider_class->stop = gst_ndi_device_provider_stop;

    gst_device_provider_class_set_static_metadata(provider_class,
        "NDI Device Provider",
        "Source/Video/Audio", "List NDI source devices",
        "teaminua.com");
}

static void
gst_ndi_device_provider_init(GstNdiDeviceProvider* provider)
{
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);

    self->priv = g_new0(GstNdiDeviceProviderPrivate, 1);
    self->priv->devices = NULL;
    self->priv->finder = g_object_new(GST_TYPE_NDI_FINDER, NULL);
    gst_ndi_finder_start(self->priv->finder);
}

static void
gst_ndi_device_provider_dispose(GObject* object) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(object);
    GST_DEBUG_OBJECT(object, "Dispose");
    if (self->priv->finder) {
        gst_ndi_finder_stop(self->priv->finder);
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
gst_ndi_device_provider_finalize(GObject* object) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(object);
    
    g_list_free(self->priv->devices);
    self->priv->devices = NULL;
    if (self->priv->finder) {
        g_object_unref(self->priv->finder);
        self->priv->finder = NULL;
    }
    g_free(self->priv);
    self->priv = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GList*
gst_ndi_device_provider_get_devices(GstNdiDeviceProvider* self)
{
    GList* list = NULL;
    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = gst_ndi_finder_get_sources(self->priv->finder, &no_sources);
    for (uint32_t i = 0; i < no_sources; i++) {
        const NDIlib_source_t* source = p_sources + i;
        GstDevice* gstDevice = gst_ndi_device_provider_create_video_src_device(source->p_ip_address, source->p_ndi_name);
        list = g_list_append(list, gstDevice);

        gstDevice = gst_ndi_device_provider_create_audio_src_device(source->p_ip_address, source->p_ndi_name);
        list = g_list_append(list, gstDevice);
    }

    return list;
}

static GList*
gst_ndi_device_provider_probe(GstDeviceProvider* provider) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    GST_DEBUG_OBJECT(self, "Probe");

    return gst_ndi_device_provider_get_devices(self);
}

static gboolean
gst_ndi_device_provider_start(GstDeviceProvider* provider) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    if (!self->priv->finder) {
        return FALSE;
    }
    GST_DEBUG_OBJECT(self, "Start");

    self->priv->devices = gst_ndi_device_provider_get_devices(self);
    for (GList* tmp = self->priv->devices; tmp; tmp = tmp->next) {
        gst_device_provider_device_add(provider, tmp->data);
    }

    gst_ndi_finder_set_callback(self->priv->finder, GST_OBJECT(provider), gst_ndi_device_provider_device_changed);
    return TRUE;
}

static void
gst_ndi_device_provider_stop(GstDeviceProvider* provider) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    GST_DEBUG_OBJECT(self, "Stop");
    gst_ndi_finder_set_callback(self->priv->finder, GST_OBJECT(provider), NULL);
}

static void
gst_ndi_device_provider_device_changed(GstObject* provider, gboolean isAdd, gchar* id, gchar* name) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    GST_INFO_OBJECT(self, "Device changed");
    
    if (isAdd) {
        GstDevice* gstDevice = gst_ndi_device_provider_create_video_src_device(id, name);
        self->priv->devices = g_list_append(self->priv->devices, gstDevice);
        gst_device_provider_device_add(GST_DEVICE_PROVIDER(provider), gstDevice);

        gstDevice = gst_ndi_device_provider_create_audio_src_device(id, name);
        self->priv->devices = g_list_append(self->priv->devices, gstDevice);
        gst_device_provider_device_add(GST_DEVICE_PROVIDER(provider), gstDevice);
    }
    else {
        for (GList* tmp = self->priv->devices; tmp; tmp = tmp->next) {
            GstElement* element = gst_device_create_element(GST_DEVICE(tmp->data), NULL);
            if (element) {
                GValue valueId = G_VALUE_INIT;
                g_value_init(&valueId, G_TYPE_STRING);
                g_object_get_property(G_OBJECT(element), "device-path", &valueId);
                const gchar* _id = g_value_get_string(&valueId);

                GValue valueName = G_VALUE_INIT;
                g_value_init(&valueName, G_TYPE_STRING);
                g_object_get_property(G_OBJECT(element), "device-name", &valueName);
                const gchar* _name = g_value_get_string(&valueName);

                if (_id && _name && strcmp(id, _id) == 0 && strcmp(name, _name) == 0) {
                    gst_device_provider_device_remove(GST_DEVICE_PROVIDER(provider), GST_DEVICE(tmp->data));
                }

                g_value_unset(&valueName);
                g_value_unset(&valueId);

                gst_object_unref(element);
            }
        }
    }
}
