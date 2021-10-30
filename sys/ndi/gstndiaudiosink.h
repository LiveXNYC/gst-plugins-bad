#ifndef __GST_NDI_AUDIO_SINK_H__
#define __GST_NDI_AUDIO_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstndioutput.h"

G_BEGIN_DECLS
#define GST_TYPE_NDI_AUDIO_SINK gst_ndi_audio_sink_get_type()
#define GST_NDI_AUDIO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NDI_AUDIO_SINK, GstNdiAudioSink))
#define GST_NDI_AUDIO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NDI_AUDIO_SINK, GstNdiAudioSinkClass))
#define GST_IS_NDI_AUDIO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NDI_AUDIO_SINK))
#define GST_IS_NDI_AUDIO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NDI_AUDIO_SINK))
#define GST_NDI_AUDIO_SINK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NDI_AUDIO_SINK, GstNdiAudioSinkClass))

typedef struct _GstNdiAudioSink        GstNdiAudioSink;
typedef struct _GstNdiAudioSinkClass   GstNdiAudioSinkClass;

struct _GstNdiAudioSink
{
    GstBaseSink sink;
    GstNdiOutput* output;
    GMutex output_mutex;

    gchar* device_name;
};

struct _GstNdiAudioSinkClass
{
    GstBaseSinkClass parent_class;
};

GType gst_ndi_audio_sink_get_type(void);

G_END_DECLS

#endif /* __GST_MF_AUDIO_SINK_H__ */

