#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndideviceprovider.h"
#include "gstndidevice.h"
#include "gstndiutil.h"

#include <Processing.NDI.Lib.h>

G_DEFINE_TYPE(GstNdiDeviceProvider, gst_ndi_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static GList* gst_ndi_device_provider_probe(GstDeviceProvider* provider);

static void
gst_ndi_device_provider_class_init(GstNdiDeviceProviderClass* klass)
{
    GstDeviceProviderClass* provider_class = GST_DEVICE_PROVIDER_CLASS(klass);

    provider_class->probe = GST_DEBUG_FUNCPTR(gst_ndi_device_provider_probe);

    gst_device_provider_class_set_static_metadata(provider_class,
        "NDI Device Provider",
        "Source/Video", "List NDI source devices",
        "teaminua.com");
}

static void
gst_ndi_device_provider_init(GstNdiDeviceProvider* provider)
{

}


static GstCaps*
gst_ndi_device_provider_get_caps_from_device(NDIlib_recv_instance_t pNDI_recv, const NDIlib_source_t* p_sources) {
    if (pNDI_recv == NULL) {
        return NULL;
    }
    // Connect to our sources
    NDIlib_recv_connect(pNDI_recv, p_sources);
    NDIlib_video_frame_v2_t video_frame = gst_ndi_util_get_video_frame(pNDI_recv, 5000);
    if (video_frame.xres > 0 && video_frame.yres > 0) {
        GstCaps* caps = gst_util_create_video_caps(&video_frame);
        NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
        return caps;
    }
    return NULL;
}

static GList*
gst_ndi_device_provider_probe(GstDeviceProvider* provider)
{
    GstNdiDeviceProvider* self = GST_NDI_DEVICE_PROVIDER(provider);
    GList* list = NULL;

    if (!NDIlib_initialize()) return list;

    // We are going to create an NDI finder that locates sources on the network.
    NDIlib_find_create_t p_create_settings;
    p_create_settings.show_local_sources = true;
    p_create_settings.p_extra_ips = NULL;
    p_create_settings.p_groups = NULL;
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2(&p_create_settings);
    if (!pNDI_find) return list;

    if (NDIlib_find_wait_for_sources(pNDI_find, 5000/* 5 seconds */)) {
        // Get the updated list of sources
        uint32_t no_sources = 0;
        const NDIlib_source_t* p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

        NDIlib_recv_instance_t pNDI_recv = NULL;
        //pNDI_recv = NDIlib_recv_create_v3(NULL);

        // Display all the sources.
        for (uint32_t i = 0; i < no_sources; i++) {
            GST_INFO("%u. %s\n", i + 1, p_sources[i].p_ndi_name);

                /* Set some useful properties */
                GstStructure* props = gst_structure_new("ndi-proplist",
                    "device.api", G_TYPE_STRING, "NDI",
                    "device.strid", G_TYPE_STRING, GST_STR_NULL(p_sources[i].p_url_address),
                    "device.friendlyName", G_TYPE_STRING, p_sources[i].p_ndi_name, NULL);

                GST_INFO("caps\n");
                GstCaps* caps = gst_ndi_device_provider_get_caps_from_device(pNDI_recv, p_sources + i);
                if (caps == NULL) {
                    caps = gst_util_create_default_videro_caps();
                }

                GstDevice* device = g_object_new(GST_TYPE_NDI_DEVICE, "device", p_sources[i].p_url_address,
                    "display-name", p_sources[i].p_ndi_name,
                    "caps", caps,
                    "device-class", "Video/Source",
                    "properties", props,
                    NULL);

                list = g_list_append(list, device);

                gst_structure_free(props);
                
                if (caps) {
                    gst_caps_unref(caps);
                }
        }
        if (pNDI_recv) {
            NDIlib_recv_destroy(pNDI_recv);
        }
    }
    
    // Destroy the NDI finder
    NDIlib_find_destroy(pNDI_find);

    return list;
}
