#ifndef __GST_NDI_UTIL_H__
#define __GST_NDI_UTIL_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>

G_BEGIN_DECLS

const gchar*
gst_ndi_util_get_format(NDIlib_FourCC_video_type_e fourCC);

const gchar*
gst_ndi_util_get_frame_format(NDIlib_frame_format_type_e frameFormat);

NDIlib_video_frame_v2_t gst_ndi_util_get_video_frame(NDIlib_recv_instance_t instance, gint timeout);
NDIlib_audio_frame_v2_t gst_ndi_util_get_audio_frame(NDIlib_recv_instance_t instance, gint timeout);
GstCaps* gst_util_create_video_caps(const NDIlib_video_frame_v2_t* frame);
GstCaps* gst_util_create_audio_caps(const NDIlib_audio_frame_v2_t* frame);
GstCaps* gst_util_create_default_video_caps(void);
GstCaps* gst_util_create_default_audio_caps(void);

G_END_DECLS

#endif /* __GST_MF_UTIL_H__ */
