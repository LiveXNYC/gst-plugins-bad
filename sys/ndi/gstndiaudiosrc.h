#ifndef __GST_NDI_AUDIO_SRC_H__
#define __GST_NDI_AUDIO_SRC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiosrc.h>

#include <ndi/Processing.NDI.Lib.h>

G_BEGIN_DECLS

#define GST_TYPE_NDI_AUDIO_SRC (gst_ndi_audio_src_get_type())
#define GST_NDI_AUDIO_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NDI_AUDIO_SRC, GstNdiAudioSrc))
#define GST_NDI_AUDIO_SRC_CAST(obj) ((GstNdiAudioSrc*)obj)
#define GST_NDI_AUDIO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NDI_AUDIO_SRC, GstNdiAudioSrcClass))
#define GST_IS_NDI_AUDIO_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NDI_AUDIO_SRC))
#define GST_IS_NDI_AUDIO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NDI_AUDIO_SRC))

typedef struct _GstNdiAudioSrc GstNdiAudioSrc;
typedef struct _GstNdiAudioSrcClass GstNdiAudioSrcClass;

struct _GstNdiAudioSrc
{
	GstAudioSrc parent;
	NDIlib_recv_instance_t pNDI_recv;
	gchar* device_path;
	gchar* device_name;
	GstAdapter* adapter;
};

struct _GstNdiAudioSrcClass
{
	GstAudioSrcClass parent_class;
};

GType gst_ndi_audio_src_get_type(void);

G_END_DECLS

#endif /* __GST_NDI_AUDIO_SRC_H__ */
