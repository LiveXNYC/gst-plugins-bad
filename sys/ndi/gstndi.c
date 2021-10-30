#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstndidevice.h"
#include "gstndideviceprovider.h"
#include "gstndivideosrc.h"
#include "gstndiaudiosrc.h"
#include "gstndivideosink.h"
#include "gstndiaudiosink.h"

GST_DEBUG_CATEGORY(gst_ndi_debug);
GST_DEBUG_CATEGORY(gst_ndi_source_object_debug);

#define GST_CAT_DEFAULT gst_ndi_debug

static gboolean
plugin_init(GstPlugin* plugin)
{
    if (!NDIlib_initialize()) return FALSE;

    GstRank rank = GST_RANK_SECONDARY;
    GST_DEBUG_CATEGORY_INIT(gst_ndi_debug, "ndi", 0, "NDI native");
    GST_DEBUG_CATEGORY_INIT(gst_ndi_source_object_debug,
        "ndisourceobject", 0, "ndisourceobject");

    gst_element_register(plugin, "ndivideosrc", GST_RANK_NONE, GST_TYPE_NDI_VIDEO_SRC);
    gst_element_register(plugin, "ndiaudiosrc", GST_RANK_NONE, GST_TYPE_NDI_AUDIO_SRC);

    gst_element_register(plugin, "ndivideosink", GST_RANK_NONE, GST_TYPE_NDI_VIDEO_SINK);
    gst_element_register(plugin, "ndiaudiosink", GST_RANK_NONE, GST_TYPE_NDI_AUDIO_SINK);

    gst_device_provider_register(plugin, "ndideviceprovider", rank, GST_TYPE_NDI_DEVICE_PROVIDER);

    return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "ndi"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ndi,
    "NDI plugin",
    plugin_init, "1.18.5", "LGPL", PACKAGE, "support@teaminua.com")
