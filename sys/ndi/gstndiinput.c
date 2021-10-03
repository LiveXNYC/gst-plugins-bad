#include "gstndiinput.h"

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

static GHashTable* inputs = NULL;

static gpointer thread_func(gpointer data);
static void gst_ndi_input_create_inputs(void);
static void gst_ndi_input_release_inputs(void);
static GstNdiInput* gst_ndi_input_create_input();

static NDIlib_recv_instance_t
gst_ndi_input_create_receiver(const gchar* url_adress) 
{
    GST_DEBUG("Create NDI receiver for %s", url_adress);

    NDIlib_recv_create_v3_t create;
    create.source_to_connect_to.p_url_address = url_adress;
    create.source_to_connect_to.p_ndi_name = "";
    create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
    create.bandwidth = NDIlib_recv_bandwidth_highest;
    create.allow_video_fields = FALSE;
    create.p_ndi_recv_name = NULL;

    NDIlib_recv_instance_t recv = NDIlib_recv_create_v3(NULL/*&create*/);
    if (recv) {
        NDIlib_source_t connection;
        connection.p_ip_address = url_adress;
        connection.p_ndi_name = NULL;
        NDIlib_recv_connect(recv, &connection);
    }

    return recv;
}

static void
gst_ndi_input_update_video_input(GstNdiInput* self, NDIlib_video_frame_v2_t* video_frame) {
    gboolean is_caps_changed = self->xres != video_frame->xres;
    is_caps_changed |= self->yres != video_frame->yres;
    is_caps_changed |= self->frame_rate_N != video_frame->frame_rate_N;
    is_caps_changed |= self->frame_rate_D != video_frame->frame_rate_D;
    is_caps_changed |= self->frame_format_type != video_frame->frame_format_type;
    is_caps_changed |= self->FourCC != video_frame->FourCC;
    is_caps_changed |= self->picture_aspect_ratio != video_frame->picture_aspect_ratio;

    self->xres = video_frame->xres;
    self->yres = video_frame->yres;
    self->frame_rate_N = video_frame->frame_rate_N;
    self->frame_rate_D = video_frame->frame_rate_D;
    self->frame_format_type = video_frame->frame_format_type;
    self->FourCC = video_frame->FourCC;
    self->stride = video_frame->line_stride_in_bytes;
    self->picture_aspect_ratio = video_frame->picture_aspect_ratio;

    if (self->got_video_frame) {
        guint size = video_frame->line_stride_in_bytes * video_frame->yres;
        self->got_video_frame(self->videosrc, (gint8*)video_frame->p_data, size, is_caps_changed);
    }
}

static void
gst_ndi_input_update_audio_input(GstNdiInput* self, NDIlib_audio_frame_v2_t* audio_frame) {
    gboolean is_caps_changed = self->channels != audio_frame->no_channels;
    is_caps_changed |= self->sample_rate != audio_frame->sample_rate;

    self->channels = audio_frame->no_channels;
    self->sample_rate = audio_frame->sample_rate;
    self->audio_buffer_size = audio_frame->no_samples * sizeof(float) * audio_frame->no_channels;
    int stride = audio_frame->no_channels == 1 ? 0 : audio_frame->channel_stride_in_bytes;
    if (self->got_audio_frame) {
        self->got_audio_frame(self->audiosrc, (gint8*)audio_frame->p_data, self->audio_buffer_size
            , stride, is_caps_changed);
    }
}

static void
gst_ndi_input_capture(GstNdiInput* self, const gchar* id) {
    if (self->pNDI_recv == NULL) {
        self->pNDI_recv = gst_ndi_input_create_receiver(id);

        if (self->pNDI_recv == NULL) {
            return;
        }
    }

    NDIlib_audio_frame_v2_t audio_frame;
    NDIlib_video_frame_v2_t video_frame;
    while (!self->is_capture_terminated) {
        NDIlib_frame_type_e res = NDIlib_recv_capture_v2(self->pNDI_recv, &video_frame, &audio_frame, NULL, 500);
        if (res == NDIlib_frame_type_video) {
            gst_ndi_input_update_video_input(self, &video_frame);
            NDIlib_recv_free_video_v2(self->pNDI_recv, &video_frame);
        }
        else if (res == NDIlib_frame_type_audio) {
            gst_ndi_input_update_audio_input(self, &audio_frame);
            NDIlib_recv_free_audio_v2(self->pNDI_recv, &audio_frame);
        }
        else if (res == NDIlib_frame_type_error) {
            GST_DEBUG("NDI receive ERROR %s", id);
        }
    }

    if (self->pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->pNDI_recv);
        self->pNDI_recv = NULL;
    }
}

static void
gst_ndi_input_capture_sync(GstNdiInput* self, const gchar* id) {
    if (self->pNDI_recv_sync == NULL) {
        self->pNDI_recv = gst_ndi_input_create_receiver(id);
        if (self->pNDI_recv == NULL) {
            return;
        }

        self->pNDI_recv_sync = NDIlib_framesync_create(self->pNDI_recv);
        if (self->pNDI_recv_sync == NULL) {
            NDIlib_recv_destroy(self->pNDI_recv);
            self->pNDI_recv = NULL;
            return;
        }
    }

    while (!self->is_capture_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;

        NDIlib_framesync_capture_video(self->pNDI_recv_sync, &video_frame, NDIlib_frame_format_type_progressive);
        if (video_frame.p_data) {
            gst_ndi_input_update_video_input(self, &video_frame);
            NDIlib_framesync_free_video(self->pNDI_recv_sync, &video_frame);
        }

        NDIlib_framesync_capture_audio(self->pNDI_recv_sync, &audio_frame, 0, 0, 0);

        if (audio_frame.p_data) {
            gst_ndi_input_update_audio_input(self, &audio_frame);
            NDIlib_framesync_free_audio(self->pNDI_recv_sync, &audio_frame);
        }

        g_usleep(33000);
    }

    if (self->pNDI_recv_sync == NULL) {
        NDIlib_framesync_destroy(self->pNDI_recv_sync);
        self->pNDI_recv_sync = NULL;
    }

    if (self->pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->pNDI_recv);
        self->pNDI_recv = NULL;
    }
}

static gpointer
input_capture_thread_func(gpointer data) {
    gchar* id = (gchar*)data;

    GST_DEBUG("START NDI CAPTURE THREAD");
    if (g_hash_table_contains(inputs, id)) {
        GstNdiInput* self = g_hash_table_lookup(inputs, id);
        self->is_started = TRUE;

        gst_ndi_input_capture(self, id);
        //gst_ndi_device_capture_sync(self, id);

        self->is_started = FALSE;
    }
    GST_DEBUG("STOP NDI CAPTURE THREAD");

    return NULL;
}

GstNdiInput*
gst_ndi_input_acquire(const char* id, GstElement* src, gboolean is_audio) {
    gst_ndi_input_create_inputs();

    GST_INFO("Acquire input. Total inputs: %d", g_hash_table_size(inputs));

    GstNdiInput* input = NULL;

    if (g_hash_table_contains(inputs, id)) {
        input = g_hash_table_lookup(inputs, id);
    }
    else {
        GST_INFO("Device input not found");
        input = gst_ndi_input_create_input();
        gchar* key = g_strdup(id);
        g_hash_table_insert(inputs, key, input);
        GST_INFO("Add input id = %s", id);
    }

    gboolean is_error = FALSE;
    if (is_audio) {
        if (input->audiosrc == NULL) {
            input->audiosrc = src;
            input->is_audio_enabled = TRUE;

            GST_INFO("Audio input is acquired");
        }
        else {
            GST_ERROR("Audio input is busy");

            is_error = TRUE;
        }
    }
    else {
        if (input->videosrc == NULL) {
            input->videosrc = src;
            input->is_video_enabled = TRUE;

            GST_INFO("Video input is acquired");
        }
        else {
            GST_ERROR("Video input is busy");

            is_error = TRUE;
        }
    }

    if (!is_error) {
        if (input->capture_thread == NULL) {

            GST_DEBUG("Start input thread");

            input->is_capture_terminated = FALSE;
            GError* error = NULL;
            input->capture_thread =
                g_thread_try_new("GstNdiInputReader", input_capture_thread_func, (gpointer)id, &error);
        }

        GST_DEBUG("ACQUIRE OK");

        return input;
    }

    GST_ERROR("Acquire failed");

    return NULL;
}

void
gst_ndi_input_release(const char* id, GstElement* src, gboolean is_audio) {
    if (g_hash_table_contains(inputs, id)) {
        GstNdiInput* input = g_hash_table_lookup(inputs, id);
        if (is_audio) {
            if (input->audiosrc == src) {
                input->audiosrc = NULL;
                input->is_audio_enabled = FALSE;

                GST_INFO("Audio input is free");
            }
        }
        else {
            if (input->videosrc == src) {
                input->videosrc = NULL;
                input->is_video_enabled = FALSE;

                GST_INFO("Video input is free");
            }
        }

        if (!input->is_video_enabled
            && !input->is_audio_enabled) {
            g_hash_table_remove(inputs, id);
            if (g_hash_table_size(inputs) == 0) {
                gst_ndi_input_release_inputs();
            }
        }
    }
}

static void
gst_ndi_input_stop_capture_thread(GstNdiInput* input) {
    if (input->capture_thread) {
        GThread* capture_thread = g_steal_pointer(&input->capture_thread);
        input->is_capture_terminated = TRUE;

        GST_DEBUG("Stop capture thread");

        g_thread_join(capture_thread);
        input->capture_thread = NULL;
    }
}

static GstNdiInput*
gst_ndi_input_create_input()
{
    GstNdiInput* input = g_new0(GstNdiInput, 1);
    g_mutex_init(&input->lock);

    return input;
}

static void
gst_ndi_input_free_input(gpointer data) {
    GstNdiInput* input = (GstNdiInput*)data;

    gst_ndi_input_stop_capture_thread(input);
    g_free(input);
}

static void
gst_ndi_input_create_inputs(void) {
    if (inputs == NULL) {
        inputs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, gst_ndi_input_free_input);
    }
}

static void
gst_ndi_input_release_inputs(void) {
    GST_DEBUG("Release inputs");
    if (!inputs) {
        return;
    }

    g_hash_table_unref(inputs);
    inputs = NULL;
}
