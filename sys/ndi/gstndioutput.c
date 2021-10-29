#include "gstndioutput.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

static GHashTable* outputs = NULL;

struct _GstNdiOutputPriv {
    GMutex lock;
	NDIlib_send_instance_t pNDI_send;

    GstElement* videosink;
    NDIlib_video_frame_v2_t NDI_video_frame;
    
    GstElement* audiosink;
};

static GstNdiOutput* current_instance = NULL;

static void
gst_ndi_input_free_output(gpointer data) {
    GstNdiOutput* output = (GstNdiOutput*)data;

    NDIlib_send_destroy(output->priv->pNDI_send);
    free((void*)output->priv->NDI_video_frame.p_data);
    g_mutex_clear(&output->priv->lock);
    g_free(output->priv);
    g_free(output);
}

static void
gst_ndi_input_create_outputs(void) {
    if (outputs == NULL) {
        outputs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, gst_ndi_input_free_output);
    }
}

static GstNdiOutput*
gst_ndi_output_create_output(const char* id)
{
    GstNdiOutput* output = NULL;

    NDIlib_send_create_t NDI_send_create_desc;
    NDI_send_create_desc.p_ndi_name = id;
    NDI_send_create_desc.p_groups = NULL;
    NDI_send_create_desc.clock_audio = TRUE;
    NDI_send_create_desc.clock_video = TRUE;

    NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
    if (pNDI_send) {
        output = g_new0(GstNdiOutput, 1);
        output->priv = g_new0(GstNdiOutputPriv, 1);
        g_mutex_init(&output->priv->lock);
        output->priv->pNDI_send = pNDI_send;
    }

    return output;
}

GstNdiOutput* 
gst_ndi_output_acquire(const char* id, GstElement* sink, gboolean is_audio) {
    gst_ndi_input_create_outputs();

    GST_INFO("Acquire output. Total outputs: %d", g_hash_table_size(outputs));

    GstNdiOutput* output = NULL;

    if (g_hash_table_contains(outputs, id)) {
        output = g_hash_table_lookup(outputs, id);
    }
    else {
        GST_INFO("Device output not found");
        output = gst_ndi_output_create_output(id);
        if (output) {
            gchar* key = g_strdup(id);
            g_hash_table_insert(outputs, key, output);
            current_instance = output;
            GST_INFO("Add output id = %s", id);
        }
    }

    return output;
}

static void
gst_ndi_output_release_outputs(void) {
    GST_DEBUG("Release outputs");
    if (!outputs) {
        return;
    }

    g_hash_table_unref(outputs);
    outputs = NULL;
}

void 
gst_ndi_output_release(const char* id, GstElement* src, gboolean is_audio) {
    if (g_hash_table_contains(outputs, id)) {
        GstNdiOutput* output = g_hash_table_lookup(outputs, id);
        if (is_audio) {
            if (output->priv->audiosink == src) {
                output->priv->audiosink = NULL;

                GST_INFO("Audio output is free");
            }
        }
        else {
            if (output->priv->videosink == src) {
                output->priv->videosink = NULL;

                GST_INFO("Video output is free");
            }
        }

        if (!output->priv->videosink
            && !output->priv->audiosink) {
            g_hash_table_remove(outputs, id);
            if (g_hash_table_size(outputs) == 0) {
                gst_ndi_output_release_outputs();
            }
        }
    }
}

void
gst_ndi_output_create_video_frame(GstNdiOutput* output, GstCaps* caps) {
    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        return FALSE;
    }

    switch (videoInfo.finfo->format) {
    case GST_VIDEO_FORMAT_UYVY:
        output->priv->NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;
        break;
    case GST_VIDEO_FORMAT_I420:
        output->priv->NDI_video_frame.FourCC = NDIlib_FourCC_video_type_I420;
        break;
    case GST_VIDEO_FORMAT_NV12:
        output->priv->NDI_video_frame.FourCC = NDIlib_FourCC_video_type_NV12;
        break;
    case GST_VIDEO_FORMAT_BGRA:
        output->priv->NDI_video_frame.FourCC = NDIlib_FourCC_video_type_BGRA;
        break;
    case GST_VIDEO_FORMAT_RGBA:
        output->priv->NDI_video_frame.FourCC = NDIlib_FourCC_video_type_RGBA;
        break;
    }

    switch (videoInfo.interlace_mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
        output->priv->NDI_video_frame.frame_format_type = NDIlib_frame_format_type_progressive;
        break;
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
        output->priv->NDI_video_frame.frame_format_type = NDIlib_frame_format_type_interleaved;
        break;
    }

    output->priv->NDI_video_frame.xres = videoInfo.width;
    output->priv->NDI_video_frame.yres = videoInfo.height;
    output->priv->NDI_video_frame.frame_rate_N = videoInfo.fps_n;
    output->priv->NDI_video_frame.frame_rate_D = videoInfo.fps_d;

    output->priv->NDI_video_frame.p_data = (uint8_t*)malloc(videoInfo.size);
}

gboolean 
gst_ndi_output_send_buffer(GstNdiOutput* output, GstBuffer* buffer) {
    /*GstVideoFrame videoFrame;
    GstVideoInfo videoInfo;
    if (!gst_video_frame_map(&videoFrame, &videoInfo, buffer, GST_MAP_READ)) {
        return GST_FLOW_ERROR;
    }

    gst_video_frame_unmap(&videoFrame);*/

    auto bufferSize = gst_buffer_get_size(buffer);
    bufferSize = gst_buffer_extract(buffer, 0, output->priv->NDI_video_frame.p_data, bufferSize);

    NDIlib_send_send_video_v2(output->priv->pNDI_send, &output->priv->NDI_video_frame);
}
