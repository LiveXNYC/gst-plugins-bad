#ifndef __GST_NDI_VIDEO_SINK_H__
#define __GST_NDI_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include "gstndioutput.h"

G_BEGIN_DECLS
#define GST_TYPE_NDI_VIDEO_SINK gst_ndi_video_sink_get_type()
#define GST_NDI_VIDEO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NDI_VIDEO_SINK, GstNdiVideoSink))
#define GST_NDI_VIDEO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NDI_VIDEO_SINK, GstNdiVideoSinkClass))
#define GST_IS_NDI_VIDEO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NDI_VIDEO_SINK))
#define GST_IS_NDI_VIDEO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NDI_VIDEO_SINK))
#define GST_NDI_VIDEO_SINK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NDI_VIDEO_SINK, GstNdiVideoSinkClass))

typedef struct _GstNdiVideoSink        GstNdiVideoSink;
typedef struct _GstNdiVideoSinkClass   GstNdiVideoSinkClass;

struct _GstNdiVideoSink
{
    GstVideoSink sink;
    GstNdiOutput* output;
    GMutex output_mutex;

    gchar* device_name;
};

struct _GstNdiVideoSinkClass
{
    GstVideoSinkClass parent_class;
};

GType gst_ndi_video_sink_get_type(void);

G_END_DECLS

#endif /* __GST_MF_VIDEO_SINK_H__ */
