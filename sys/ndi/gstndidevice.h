#ifndef __GST_NDI_DEVICE_H__
#define __GST_NDI_DEVICE_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>
#include <gst/base/base.h>

G_BEGIN_DECLS

typedef struct _GstNdiDevice GstNdiDevice;
typedef struct _GstNdiDeviceClass GstNdiDeviceClass;

#define GST_TYPE_NDI_DEVICE          (gst_ndi_device_get_type())
#define GST_NDI_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NDI_DEVICE,GstNdiDevice))

struct _GstNdiDeviceClass
{
    GstDeviceClass parent_class;
};

struct _GstNdiDevice
{
    GstDevice parent;

    gchar* device_path;
    gchar* device_name;
    gboolean isVideo;
};

GstDevice*
gst_ndi_device_provider_create_video_src_device(const char* id, const char* name);
GstDevice*
gst_ndi_device_provider_create_audio_src_device(const char* id, const char* name);

GType gst_ndi_device_get_type(void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstNdiDevice, gst_object_unref)

G_END_DECLS

#endif /* __GST_NDI_DEVICE_H__ */
