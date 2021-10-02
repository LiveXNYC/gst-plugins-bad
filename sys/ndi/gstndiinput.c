#include "gstndiinput.h"

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

typedef struct _Input Input;
struct _Input
{
    gchar* id;
    gchar* p_ndi_name;
    GstNdiInput input;
};

static GPtrArray* inputs = NULL;

static gpointer thread_func(gpointer data);
static void gst_ndi_input_create_inputs(void);
static void gst_ndi_input_release_inputs(void);

static Input*
gst_ndi_input_create_input(const gchar* p_url_address, const gchar* p_ndi_name)
{
    Input* input = g_new0(Input, 1);
    input->id = g_strdup(p_url_address);
    input->p_ndi_name = g_strdup(p_ndi_name);
    g_mutex_init(&input->input.lock);

    return input;
}

static NDIlib_recv_instance_t
gst_ndi_input_create_receiver(const gchar* url_adress, const gchar* name) 
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
        connection.p_ndi_name = name;
        NDIlib_recv_connect(recv, &connection);
    }

    return recv;
}

static void
gst_ndi_input_update_video_input(Input* self, NDIlib_video_frame_v2_t* video_frame) {
    gboolean is_caps_changed = self->input.xres != video_frame->xres;
    is_caps_changed |= self->input.yres != video_frame->yres;
    is_caps_changed |= self->input.frame_rate_N != video_frame->frame_rate_N;
    is_caps_changed |= self->input.frame_rate_D != video_frame->frame_rate_D;
    is_caps_changed |= self->input.frame_format_type != video_frame->frame_format_type;
    is_caps_changed |= self->input.FourCC != video_frame->FourCC;
    is_caps_changed |= self->input.picture_aspect_ratio != video_frame->picture_aspect_ratio;

    self->input.xres = video_frame->xres;
    self->input.yres = video_frame->yres;
    self->input.frame_rate_N = video_frame->frame_rate_N;
    self->input.frame_rate_D = video_frame->frame_rate_D;
    self->input.frame_format_type = video_frame->frame_format_type;
    self->input.FourCC = video_frame->FourCC;
    self->input.stride = video_frame->line_stride_in_bytes;
    self->input.picture_aspect_ratio = video_frame->picture_aspect_ratio;

    if (self->input.got_video_frame) {
        guint size = video_frame->line_stride_in_bytes * video_frame->yres;
        self->input.got_video_frame(self->input.videosrc, (gint8*)video_frame->p_data, size, is_caps_changed);
    }
}

static void
gst_ndi_input_update_audio_input(Input* self, NDIlib_audio_frame_v2_t* audio_frame) {
    gboolean is_caps_changed = self->input.channels != audio_frame->no_channels;
    is_caps_changed |= self->input.sample_rate != audio_frame->sample_rate;

    self->input.channels = audio_frame->no_channels;
    self->input.sample_rate = audio_frame->sample_rate;
    self->input.audio_buffer_size = audio_frame->no_samples * sizeof(float) * audio_frame->no_channels;
    int stride = audio_frame->no_channels == 1 ? 0 : audio_frame->channel_stride_in_bytes;
    if (self->input.got_audio_frame) {
        self->input.got_audio_frame(self->input.audiosrc, (gint8*)audio_frame->p_data, self->input.audio_buffer_size
            , stride, is_caps_changed);
    }
}

static void
gst_ndi_input_capture(Input* self) {
    if (self->input.pNDI_recv == NULL) {
        self->input.pNDI_recv = gst_ndi_input_create_receiver(self->id, self->p_ndi_name);

        if (self->input.pNDI_recv == NULL) {
            return;
        }
    }

    NDIlib_audio_frame_v2_t audio_frame;
    NDIlib_video_frame_v2_t video_frame;
    while (!self->input.is_capture_terminated) {
        NDIlib_frame_type_e res = NDIlib_recv_capture_v2(self->input.pNDI_recv, &video_frame, &audio_frame, NULL, 500);
        if (res == NDIlib_frame_type_video) {
            gst_ndi_input_update_video_input(self, &video_frame);
            NDIlib_recv_free_video_v2(self->input.pNDI_recv, &video_frame);
        }
        else if (res == NDIlib_frame_type_audio) {
            gst_ndi_input_update_audio_input(self, &audio_frame);
            NDIlib_recv_free_audio_v2(self->input.pNDI_recv, &audio_frame);
        }
        else if (res == NDIlib_frame_type_error) {
            GST_DEBUG("NDI receive ERROR %s", self->id);
        }
    }

    if (self->input.pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->input.pNDI_recv);
        self->input.pNDI_recv = NULL;
    }
}

static void
gst_ndi_input_capture_sync(Input* self) {
    if (self->input.pNDI_recv_sync == NULL) {
        self->input.pNDI_recv = gst_ndi_input_create_receiver(self->id, self->p_ndi_name);
        if (self->input.pNDI_recv == NULL) {
            return;
        }

        self->input.pNDI_recv_sync = NDIlib_framesync_create(self->input.pNDI_recv);
        if (self->input.pNDI_recv_sync == NULL) {
            NDIlib_recv_destroy(self->input.pNDI_recv);
            self->input.pNDI_recv = NULL;
            return;
        }
    }

    while (!self->input.is_capture_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;

        NDIlib_framesync_capture_video(self->input.pNDI_recv_sync, &video_frame, NDIlib_frame_format_type_progressive);
        if (video_frame.p_data) {
            gst_ndi_input_update_video_input(self, &video_frame);
            NDIlib_framesync_free_video(self->input.pNDI_recv_sync, &video_frame);
        }

        NDIlib_framesync_capture_audio(self->input.pNDI_recv_sync, &audio_frame, 0, 0, 0);

        if (audio_frame.p_data) {
            gst_ndi_input_update_audio_input(self, &audio_frame);
            NDIlib_framesync_free_audio(self->input.pNDI_recv_sync, &audio_frame);
        }

        g_usleep(33000);
    }

    if (self->input.pNDI_recv_sync == NULL) {
        NDIlib_framesync_destroy(self->input.pNDI_recv_sync);
        self->input.pNDI_recv_sync = NULL;
    }

    if (self->input.pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->input.pNDI_recv);
        self->input.pNDI_recv = NULL;
    }
}

static gpointer
input_capture_thread_func(gpointer data) {
    Input* self = (Input*)data;

    GST_DEBUG("START NDI CAPTURE THREAD");

    self->input.is_started = TRUE;

    gst_ndi_input_capture(self);
    //gst_ndi_device_capture_sync(self);

    self->input.is_started = FALSE;

    GST_DEBUG("STOP NDI CAPTURE THREAD");

    return NULL;
}

GstNdiInput*
gst_ndi_input_acquire(const char* id, GstElement* src, gboolean is_audio) {
    gst_ndi_input_create_inputs();

    GST_INFO("Acquire input. Total inputs: %d", inputs->len);

    Input* input = NULL;
    gboolean is_found = FALSE;
    gboolean is_error = FALSE;
    for (guint i = 0; i < inputs->len; ++i) {
        input = (Input*)g_ptr_array_index(inputs, i);
        if (strcmp(input->id, id) == 0) {
            is_found = TRUE;
            break;
        }
    }

    if (!is_found) {
        GST_INFO("Device input not found");
        input = gst_ndi_input_create_input(id, "");
        g_ptr_array_add(inputs, input);
        GST_INFO("Add input id = %s, name = %s", input->id, input->p_ndi_name);
    }

    if (is_audio) {
        if (input->input.audiosrc == NULL) {
            input->input.audiosrc = src;
            input->input.is_audio_enabled = TRUE;

            GST_INFO("Audio input is acquired");
        }
        else {
            GST_ERROR("Audio input is busy");

            is_error = TRUE;
        }
    }
    else {
        if (input->input.videosrc == NULL) {
            input->input.videosrc = src;
            input->input.is_video_enabled = TRUE;

            GST_INFO("Video input is acquired");
        }
        else {
            GST_ERROR("Video input is busy");

            is_error = TRUE;
        }
    }

    if (!is_error) {
        if (input->input.capture_thread == NULL) {

            GST_DEBUG("Start input thread");

            input->input.is_capture_terminated = FALSE;
            GError* error = NULL;
            input->input.capture_thread =
                g_thread_try_new("GstNdiInputReader", input_capture_thread_func, (gpointer)input, &error);
        }

        GST_DEBUG("ACQUIRE OK");

        return &input->input;
    }

    GST_ERROR("Acquire failed");

    return NULL;
}

void
gst_ndi_input_release(const char* id, GstElement* src, gboolean is_audio) {
    for (guint i = 0; i < inputs->len; ++i) {
        Input* input = (Input*)g_ptr_array_index(inputs, i);
        if (strcmp(input->id, id) == 0) {
            if (is_audio) {
                if (input->input.audiosrc == src) {
                    input->input.audiosrc = NULL;
                    input->input.is_audio_enabled = FALSE;

                    GST_INFO("Audio input is free");
                }
            }
            else {
                if (input->input.videosrc == src) {
                    input->input.videosrc = NULL;
                    input->input.is_video_enabled = FALSE;

                    GST_INFO("Video input is free");
                }
            }

            if (!input->input.is_video_enabled
                && !input->input.is_audio_enabled) {
                g_ptr_array_remove(inputs, input);
                if (inputs->len == 0) {
                    gst_ndi_input_release_inputs();
                }
            }

            break;
        }
    }
}

static void
gst_ndi_input_stop_capture_thread(Input* input) {
    if (input->input.capture_thread) {
        GThread* capture_thread = g_steal_pointer(&input->input.capture_thread);
        input->input.is_capture_terminated = TRUE;

        GST_DEBUG("Stop capture thread");

        g_thread_join(capture_thread);
        input->input.capture_thread = NULL;
    }
}

static void
gst_ndi_input_free_input(gpointer data) {
    Input* input = (Input*)data;

    GST_DEBUG("Release input id = %s", input->id);
    gst_ndi_input_stop_capture_thread(input);
    g_free(input->id);
    g_free(input->p_ndi_name);
    g_free(input);
}

static void
gst_ndi_input_create_inputs(void) {
    if (inputs == NULL) {
        inputs = g_ptr_array_new_with_free_func(gst_ndi_input_free_input);
    }
}

static void
gst_ndi_input_release_inputs(void) {
    GST_DEBUG("Release inputs");
    if (!inputs) {
        return;
    }

    g_ptr_array_unref(inputs);
    inputs = NULL;
}
