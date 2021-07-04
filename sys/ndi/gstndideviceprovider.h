#ifndef _GST_NDI_DEVICE_PROVIDER_H_
#define _GST_NDI_DEVICE_PROVIDER_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_NDI_DEVICE_PROVIDER gst_ndi_device_provider_get_type()
#define GST_NDI_DEVICE_PROVIDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NDI_DEVICE_PROVIDER,GstNdiDeviceProvider))

typedef struct _GstNdiDeviceProvider GstNdiDeviceProvider;
typedef struct _GstNdiDeviceProviderClass GstNdiDeviceProviderClass;

struct _GstNdiDeviceProviderClass
{
	GstDeviceProviderClass parent_class;
};

struct _GstNdiDeviceProvider
{
	GstDeviceProvider	parent;
	GThread*			thread;
	gboolean			isTerminated;

	GMutex              list_lock;
	GCond               list_cond;
};

GType gst_ndi_device_provider_get_type(void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstNdiDeviceProvider, gst_object_unref)

G_END_DECLS

#endif /* _GST_NDI_DEVICE_PROVIDER_H_ */

