#ifndef __GST_NDI_INPUT_H__
#define __GST_NDI_INPUT_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>

typedef struct _GstNdiInput GstNdiInput;
typedef struct _GstNdiInputPriv GstNdiInputPriv;

struct _GstNdiInput {
    /* Set by the video source */
    void (*got_video_frame) (GstElement* ndi_device, gint8* buffer, guint size, gboolean is_caps_changed, void* id);

    /* Set by the audio source */
    void (*got_audio_frame) (GstElement* ndi_device, gint8* buffer, guint size, guint stride, gboolean is_caps_changed);

    GstNdiInputPriv* priv;
};

GstNdiInput* gst_ndi_input_acquire(const char* id, GstElement* src, gboolean is_audio);
void         gst_ndi_input_release(const char* id, GstElement* src, gboolean is_audio);

GstCaps*     gst_ndi_input_get_video_caps(GstNdiInput* input);
int gst_ndi_input_get_frame_rate_n(GstNdiInput* input);
int gst_ndi_input_get_frame_rate_d(GstNdiInput* input);
GstClockTime gst_ndi_input_get_video_buffer_duration(GstNdiInput* input);
void gst_ndi_input_get_video_buffer(GstNdiInput* input, void* id, guint8** buffer, guint* size);
void gst_ndi_input_release_video_buffer(GstNdiInput* input, void* id);

GstCaps* gst_ndi_input_get_audio_caps(GstNdiInput* input);
GstClockTime gst_ndi_input_get_audio_buffer_duration(GstNdiInput* input);
int gst_ndi_input_get_channels(GstNdiInput* input);
guint gst_ndi_input_get_audio_buffer_size(GstNdiInput* input);

#endif /* __GST_NDI_INPUT_H__ */
