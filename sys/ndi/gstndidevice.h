#ifndef __GST_NDI_DEVICE_H__
#define __GST_NDI_DEVICE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_NDI_DEVICE          (gst_ndi_device_get_type())
#define GST_NDI_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NDI_DEVICE,GstNdiDevice))

typedef struct _GstNdiDevice GstNdiDevice;
typedef struct _GstNdiDeviceClass GstNdiDeviceClass;

struct _GstNdiDeviceClass
{
    GstDeviceClass parent_class;
};

struct _GstNdiDevice
{
    GstDevice parent;
    gchar* device_path;
    gboolean isVideo;
};

GstDevice*
gst_ndi_device_provider_create_device(const char* id, const char* name, gboolean isVideo);

GType gst_ndi_device_get_type(void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstNdiDevice, gst_object_unref)

G_END_DECLS

#endif /* __GST_MF_DEVICE_H__ */
