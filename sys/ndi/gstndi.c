#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstndidevice.h"
#include "gstndideviceprovider.h"
#include "gstndivideosrc.h"

GST_DEBUG_CATEGORY(gst_ndi_debug);
GST_DEBUG_CATEGORY(gst_ndi_source_object_debug);

#define GST_CAT_DEFAULT gst_ndi_debug

static void
plugin_deinit(gpointer data)
{
}

static gboolean
plugin_init(GstPlugin* plugin)
{
    GstRank rank = GST_RANK_SECONDARY;

    /**
     * plugin-mediafoundation:
     *
     * Since: 1.18
     */

    GST_DEBUG_CATEGORY_INIT(gst_ndi_debug, "ndi", 0, "NDI native");
    GST_DEBUG_CATEGORY_INIT(gst_ndi_source_object_debug,
        "ndisourceobject", 0, "ndisourceobject");

    gst_element_register(plugin, "ndivideosrc", GST_RANK_NONE,
        GST_TYPE_NDI_VIDEO_SRC);


    //gst_element_register(plugin, "ndivideosrc", rank, GST_TYPE_NDI_VIDEO_SRC);
    gst_device_provider_register(plugin, "ndideviceprovider",
        rank, GST_TYPE_NDI_DEVICE_PROVIDER);

    /* So that call MFShutdown() when this plugin is no more used
     * (i.e., gst_deinit). Otherwise valgrind-like tools would complain
     * about un-released media foundation resources.
     *
     * NOTE: MFStartup and MFShutdown can be called multiple times, but the number
     * of each MFStartup and MFShutdown call should be identical. This rule is
     * simliar to that of CoInitialize/CoUninitialize pair */
    g_object_set_data_full(G_OBJECT(plugin),
        "plugin-ndi-shutdown", "shutdown-data",
        (GDestroyNotify)plugin_deinit);

    return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "ndi"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ndi,
    "NDI plugin",
    plugin_init, "1.18.4", "LGPL", PACKAGE, "support@teaminua.com")
