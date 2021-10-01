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
gst_ndi_device_provider_device_changed(GstNdiFinder* finder, GstObject* provider);
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
    GST_INFO_OBJECT(object, "Dispose");
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
gst_ndi_device_provider_probe(GstDeviceProvider* provider) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    GST_INFO_OBJECT(self, "Probe");
    return gst_ndi_device_get_devices(self->priv->finder);
}

static gboolean
gst_ndi_device_provider_start(GstDeviceProvider* provider) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    if (!self->priv->finder) {
        return FALSE;
    }
    GST_DEBUG_OBJECT(self, "Start");

    self->priv->devices = gst_ndi_device_get_devices(self->priv->finder);
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
gst_ndi_device_provider_device_changed(GstNdiFinder* finder, GstObject* provider) {
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    GST_INFO_OBJECT(self, "Device changed");

    GList* tmp = gst_ndi_device_get_devices(finder);
    g_list_free(tmp);
}
