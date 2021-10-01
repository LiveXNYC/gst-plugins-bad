#ifndef _GST_NDI_DEVICE_PROVIDER_H_
#define _GST_NDI_DEVICE_PROVIDER_H_

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstNdiDeviceProvider GstNdiDeviceProvider;
typedef struct _GstNdiDeviceProviderPrivate GstNdiDeviceProviderPrivate;
typedef struct _GstNdiDeviceProviderClass GstNdiDeviceProviderClass;

#define GST_TYPE_NDI_DEVICE_PROVIDER gst_ndi_device_provider_get_type()
#define GST_NDI_DEVICE_PROVIDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NDI_DEVICE_PROVIDER,GstNdiDeviceProvider))

struct _GstNdiDeviceProvider
{
	GstDeviceProvider	parent;
	GstNdiDeviceProviderPrivate* priv;
};

struct _GstNdiDeviceProviderClass
{
	GstDeviceProviderClass parent_class;
};

GType gst_ndi_device_provider_get_type(void);

G_END_DECLS

#endif /* _GST_NDI_DEVICE_PROVIDER_H_ */

