#ifndef __GST_NDI_VIDEO_SRC_H__
#define __GST_NDI_VIDEO_SRC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS
#define GST_TYPE_NDI_VIDEO_SRC gst_ndi_video_src_get_type()
#define GST_NDI_VIDEO_SRC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NDI_VIDEO_SRC, GstNdiVideoSrc))
#define GST_NDI_VIDEO_SRC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NDI_VIDEO_SRC, GstNdiVideoSrcClass))
#define GST_IS_NDI_VIDEO_SRC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NDI_VIDEO_SRC))
#define GST_IS_NDI_VIDEO_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NDI_VIDEO_SRC))
#define GST_NDI_VIDEO_SRC_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NDI_VIDEO_SRC, GstNdiVideoSrcClass))

typedef struct _GstNdiVideoSrc        GstNdiVideoSrc;
typedef struct _GstNdiVideoSrcClass   GstNdiVideoSrcClass;

struct _GstNdiVideoSrc
{
    GstPushSrc parent;

    GstNdiInput* input;
    GMutex input_mutex;
    gchar* device_path;
    gchar* device_name;
    GstCaps* caps;

    GAsyncQueue* queue;
    GstBuffer* last_buffer;
    guint64 n_frames;
    GstClockTime timestamp_offset;
    gboolean is_eos;
    guint64 buffer_duration;
};

struct _GstNdiVideoSrcClass
{
    GstPushSrcClass parent_class;
};

GType gst_ndi_video_src_get_type(void);
/*G_DECLARE_FINAL_TYPE (GstNdiVideoSrc, gst_ndi_video_src, GST, NDI_VIDEO_SRC,
    GstPushSrc);*/

G_END_DECLS

#endif /* __GST_MF_VIDEO_SRC_H__ */
