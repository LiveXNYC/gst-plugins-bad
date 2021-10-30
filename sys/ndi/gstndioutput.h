#ifndef __GST_NDI_OUTPUT_H__
#define __GST_NDI_OUTPUT_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>

typedef struct _GstNdiOutput GstNdiOutput;
typedef struct _GstNdiOutputPriv GstNdiOutputPriv;

struct _GstNdiOutput {
    GstNdiOutputPriv* priv;
};

GstNdiOutput* gst_ndi_output_acquire(const char* id, GstElement* sink, gboolean is_audio);
void          gst_ndi_output_release(const char* id, GstElement* sink, gboolean is_audio);
gboolean      gst_ndi_output_create_video_frame(GstNdiOutput* output, GstCaps* caps);
gboolean      gst_ndi_output_send_video_buffer(GstNdiOutput* output, GstBuffer* buffer);
gboolean      gst_ndi_output_create_audio_frame(GstNdiOutput* output, GstCaps* caps);
gboolean      gst_ndi_output_send_audio_buffer(GstNdiOutput* output, GstBuffer* buffer);
#endif /* __GST_NDI_OUTPUT_H__ */