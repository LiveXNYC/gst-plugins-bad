#ifndef __GST_NDI_INPUT_H__
#define __GST_NDI_INPUT_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>

typedef struct _GstNdiInput GstNdiInput;
struct _GstNdiInput {
    GMutex lock;
    GThread* capture_thread;
    gboolean is_capture_terminated;

    gboolean is_started;
    NDIlib_recv_instance_t pNDI_recv;
    NDIlib_framesync_instance_t pNDI_recv_sync;

    /* Set by the video source */
    void (*got_video_frame) (GstElement* ndi_device, gint8* buffer, guint size, gboolean is_caps_changed);
    GstElement* videosrc;
    gboolean is_video_enabled;
    int xres;
    int yres;
    int frame_rate_N;
    int frame_rate_D;
    float picture_aspect_ratio;
    NDIlib_frame_format_type_e frame_format_type;
    NDIlib_FourCC_video_type_e FourCC;
    int stride;

    /* Set by the audio source */
    void (*got_audio_frame) (GstElement* ndi_device, gint8* buffer, guint size, guint stride, gboolean is_caps_changed);
    GstElement* audiosrc;
    gboolean is_audio_enabled;
    guint channels;
    guint sample_rate;
    guint audio_buffer_size;
};

GstNdiInput* gst_ndi_input_acquire(const char* id, GstElement* src, gboolean is_audio);
void         gst_ndi_input_release(const char* id, GstElement* src, gboolean is_audio);

#endif /* __GST_NDI_INPUT_H__ */