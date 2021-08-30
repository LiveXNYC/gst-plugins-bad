#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndideviceprovider.h"
#include "gstndidevice.h"
#include "gstndiutil.h"

G_DEFINE_TYPE(GstNdiDeviceProvider, gst_ndi_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static GList* gst_ndi_device_provider_probe(GstDeviceProvider* provider);

static void
gst_ndi_device_provider_class_init(GstNdiDeviceProviderClass* klass)
{
    GstDeviceProviderClass* provider_class = GST_DEVICE_PROVIDER_CLASS(klass);
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    provider_class->probe = GST_DEBUG_FUNCPTR(gst_ndi_device_provider_probe);

    gst_device_provider_class_set_static_metadata(provider_class,
        "NDI Device Provider",
        "Source/Video/Audio", "List NDI source devices",
        "teaminua.com");
}

static void
gst_ndi_device_provider_init(GstNdiDeviceProvider* provider)
{
}

static GList*
gst_ndi_device_provider_probe(GstDeviceProvider* provider) {
    return gst_decklink_get_devices();
}
