#ifndef __GST_NDI_UTIL_H__
#define __GST_NDI_UTIL_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <ndi/Processing.NDI.Lib.h>

#define GST_NDI_VIDEO_FORMATS "{ UYVY, BGRA, RGBA, I420, NV12 }"
#define NDI_VIDEO_TEMPLATE_CAPS GST_VIDEO_CAPS_MAKE (GST_NDI_VIDEO_FORMATS)

#define NDI_AUDIO_TEMPLATE_CAPS ("audio/x-raw, format=F32LE, channels=[1, 16], rate={44100, 48000, 96000}, layout=interleaved")

G_BEGIN_DECLS

const gchar* gst_ndi_util_get_format(NDIlib_FourCC_video_type_e fourCC);
const gchar* gst_ndi_util_get_frame_format(NDIlib_frame_format_type_e frameFormat);
GstCaps* gst_util_create_default_video_caps(void);
GstCaps* gst_util_create_default_audio_caps(void);

G_END_DECLS

#endif /* __GST_MF_UTIL_H__ */
