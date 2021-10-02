#ifndef __GST_NDI_FINDER_H__
#define __GST_NDI_FINDER_H__

#include <ndi/Processing.NDI.Lib.h>
#include <gst/gstelement.h>

G_BEGIN_DECLS

typedef struct _GstNdiFinder GstNdiFinder;
typedef struct _GstNdiFinderClass GstNdiFinderClass;
typedef struct _GstNdiFinderPrivate GstNdiFinderPrivate;

typedef void(*Device_Changed)(GstObject* provider, gboolean isAdd, gchar* id, gchar* name);

#define GST_TYPE_NDI_FINDER                 (gst_ndi_finder_get_type())
#define GST_IS_NDI_FINDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_NDI_FINDER))
#define GST_IS_NDI_FINDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_NDI_FINDER))
#define GST_NDI_FINDER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_NDI_FINDER, GstNdiFinderClass))
#define GST_NDI_FINDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_NDI_FINDER, GstNdiFinder))
#define GST_NDI_FINDER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_NDI_FINDER, GstNdiFinderClass))
#define GST_NDI_FINDER_CAST(obj)            ((GstNdiFinder *)(obj))

struct _GstNdiFinder {
	GstObject         parent;

	GstNdiFinderPrivate* priv;
	
	gpointer _gst_reserved[GST_PADDING];
};

struct _GstNdiFinderClass {
	GstObjectClass    parent_class;

	/*< private >*/
	gpointer _gst_reserved[GST_PADDING];
};


void gst_ndi_finder_start(GstNdiFinder* finder);
void gst_ndi_finder_stop(GstNdiFinder* finder);
const NDIlib_source_t* gst_ndi_finder_get_sources(GstNdiFinder* finder, uint32_t* no_sources);
void gst_ndi_finder_set_callback(GstNdiFinder* finder, GstObject* provider, Device_Changed callback);

GType  gst_ndi_finder_get_type(void);

G_END_DECLS

#endif /* __GST_NDI_FINDER_H__ */
