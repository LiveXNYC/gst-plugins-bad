#include "gstndiinput.h"
#include "gstndiutil.h"

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

struct _GstNdiInputPriv {
    GMutex lock;
    GThread* capture_thread;
    gboolean is_capture_terminated;

    gboolean is_started;
    NDIlib_recv_instance_t pNDI_recv;
    NDIlib_framesync_instance_t pNDI_recv_sync;

    /* Set by the video source */
    GstElement* videosrc;
    //GAsyncQueue* queue;
    int xres;
    int yres;
    int frame_rate_N;
    int frame_rate_D;
    float picture_aspect_ratio;
    NDIlib_frame_format_type_e frame_format_type;
    NDIlib_FourCC_video_type_e FourCC;
    int stride;

    /* Set by the audio source */
    GstElement* audiosrc;
    guint channels;
    guint sample_rate;
    guint audio_buffer_size;
};

static GHashTable* inputs = NULL;

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
    GstNdiInputPriv* priv = self->priv;

    gboolean is_caps_changed = priv->xres != video_frame->xres;
    is_caps_changed |= priv->yres != video_frame->yres;
    is_caps_changed |= priv->frame_rate_N != video_frame->frame_rate_N;
    is_caps_changed |= priv->frame_rate_D != video_frame->frame_rate_D;
    is_caps_changed |= priv->frame_format_type != video_frame->frame_format_type;
    is_caps_changed |= priv->FourCC != video_frame->FourCC;
    is_caps_changed |= priv->picture_aspect_ratio != video_frame->picture_aspect_ratio;

    g_mutex_lock(&priv->lock);
    priv->xres = video_frame->xres;
    priv->yres = video_frame->yres;
    priv->frame_rate_N = video_frame->frame_rate_N;
    priv->frame_rate_D = video_frame->frame_rate_D;
    priv->frame_format_type = video_frame->frame_format_type;
    priv->FourCC = video_frame->FourCC;
    priv->stride = video_frame->line_stride_in_bytes;
    priv->picture_aspect_ratio = video_frame->picture_aspect_ratio;
    g_mutex_unlock(&priv->lock);

    if (self->got_video_frame) {
        guint size = video_frame->line_stride_in_bytes * video_frame->yres;
        self->got_video_frame(priv->videosrc, (gint8*)video_frame->p_data, size, is_caps_changed, (void*)video_frame);
    }
    else {
    	gst_ndi_input_release_video_buffer(self, (void*)video_frame);
    }
}

static void
gst_ndi_input_update_audio_input(GstNdiInput* self, NDIlib_audio_frame_v2_t* audio_frame) {
    GstNdiInputPriv* priv = self->priv;

    gboolean is_caps_changed = priv->channels != audio_frame->no_channels;
    is_caps_changed |= priv->sample_rate != audio_frame->sample_rate;

    g_mutex_lock(&priv->lock);
    priv->channels = audio_frame->no_channels;
    priv->sample_rate = audio_frame->sample_rate;
    priv->audio_buffer_size = audio_frame->no_samples * sizeof(float) * audio_frame->no_channels;
    g_mutex_unlock(&priv->lock);

    int stride = audio_frame->no_channels == 1 ? 0 : audio_frame->channel_stride_in_bytes;
    if (self->got_audio_frame) {
        self->got_audio_frame(priv->audiosrc, (gint8*)audio_frame->p_data, priv->audio_buffer_size
            , stride, is_caps_changed);
    }
}

static void
gst_ndi_input_capture(GstNdiInput* self, const gchar* id) {
    GstNdiInputPriv* priv = self->priv;
    if (priv->pNDI_recv == NULL) {
        priv->pNDI_recv = gst_ndi_input_create_receiver(id);

        if (priv->pNDI_recv == NULL) {
            return;
        }
    }

    //priv->queue = g_async_queue_new();

    NDIlib_audio_frame_v2_t audio_frame;
    while (!priv->is_capture_terminated) {
        NDIlib_video_frame_v2_t* video_frame = (priv->videosrc == NULL) ? NULL : (NDIlib_video_frame_v2_t*)g_malloc0(sizeof(NDIlib_video_frame_v2_t));
        NDIlib_frame_type_e res = NDIlib_recv_capture_v2(priv->pNDI_recv, video_frame, &audio_frame, NULL, 500);
        if (res == NDIlib_frame_type_video) {
            if ((priv->videosrc != NULL)) {
                //g_async_queue_push(priv->queue, video_frame);
                gst_ndi_input_update_video_input(self, video_frame);
                //gst_ndi_input_release_video_buffer(self, video_frame);
            }
        }
        else {
            if (video_frame != NULL) {
                g_free(video_frame);
            }

            if (res == NDIlib_frame_type_audio) {
                gst_ndi_input_update_audio_input(self, &audio_frame);
                NDIlib_recv_free_audio_v2(priv->pNDI_recv, &audio_frame);
            }
            else if (res == NDIlib_frame_type_error) {
                GST_DEBUG("NDI receive ERROR %s", id);
            }
        }
    }

    /*if (priv->queue) {
        while (g_async_queue_length(priv->queue) > 0) {
            NDIlib_video_frame_v2_t* video_frame = (NDIlib_video_frame_v2_t*)g_async_queue_pop(priv->queue);
            NDIlib_recv_free_video_v2(priv->pNDI_recv, video_frame);
            g_free(video_frame);
        }
        g_async_queue_unref(priv->queue);
        priv->queue = NULL;
    }*/

    if (priv->pNDI_recv != NULL) {
        NDIlib_recv_destroy(priv->pNDI_recv);
        priv->pNDI_recv = NULL;
    }
}

static void
gst_ndi_input_capture_sync(GstNdiInput* self, const gchar* id) {
    GstNdiInputPriv* priv = self->priv;
    if (priv->pNDI_recv_sync == NULL) {
        priv->pNDI_recv = gst_ndi_input_create_receiver(id);
        if (priv->pNDI_recv == NULL) {
            return;
        }

        priv->pNDI_recv_sync = NDIlib_framesync_create(priv->pNDI_recv);
        if (priv->pNDI_recv_sync == NULL) {
            NDIlib_recv_destroy(priv->pNDI_recv);
            priv->pNDI_recv = NULL;
            return;
        }
    }

    while (!priv->is_capture_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;

        NDIlib_framesync_capture_video(priv->pNDI_recv_sync, &video_frame, NDIlib_frame_format_type_progressive);
        if (video_frame.p_data) {
            gst_ndi_input_update_video_input(self, &video_frame);
            NDIlib_framesync_free_video(priv->pNDI_recv_sync, &video_frame);
        }

        NDIlib_framesync_capture_audio(priv->pNDI_recv_sync, &audio_frame, 0, 0, 0);

        if (audio_frame.p_data) {
            gst_ndi_input_update_audio_input(self, &audio_frame);
            NDIlib_framesync_free_audio(priv->pNDI_recv_sync, &audio_frame);
        }

        g_usleep(33000);
    }

    if (priv->pNDI_recv_sync == NULL) {
        NDIlib_framesync_destroy(priv->pNDI_recv_sync);
        priv->pNDI_recv_sync = NULL;
    }

    if (priv->pNDI_recv != NULL) {
        NDIlib_recv_destroy(priv->pNDI_recv);
        priv->pNDI_recv = NULL;
    }
}

static gpointer
input_capture_thread_func(gpointer data) {
    gchar* id = (gchar*)data;

    GST_DEBUG("START NDI CAPTURE THREAD");
    if (g_hash_table_contains(inputs, id)) {
        GstNdiInput* self = g_hash_table_lookup(inputs, id);
        self->priv->is_started = TRUE;

        gst_ndi_input_capture(self, id);
        //gst_ndi_device_capture_sync(self, id);

        self->priv->is_started = FALSE;
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
        if (input->priv->audiosrc == NULL) {
            input->priv->audiosrc = src;

            GST_INFO("Audio input is acquired");
        }
        else {
            GST_ERROR("Audio input is busy");

            is_error = TRUE;
        }
    }
    else {
        if (input->priv->videosrc == NULL) {
            input->priv->videosrc = src;

            GST_INFO("Video input is acquired");
        }
        else {
            GST_ERROR("Video input is busy");

            is_error = TRUE;
        }
    }

    if (!is_error) {
        if (input->priv->capture_thread == NULL) {

            GST_DEBUG("Start input thread");

            input->priv->is_capture_terminated = FALSE;
            GError* error = NULL;
            input->priv->capture_thread =
                g_thread_try_new("GstNdiInputReader", input_capture_thread_func, (gpointer)id, &error);
        }

        GST_DEBUG("ACQUIRE OK");

        return input;
    }

    GST_ERROR("Acquire failed");

    return NULL;
}

void
gst_ndi_input_release(const char* id, GstElement* src, gboolean is_audio) 
{
    if (g_hash_table_contains(inputs, id)) {
        GstNdiInput* input = g_hash_table_lookup(inputs, id);
        if (is_audio) {
            if (input->priv->audiosrc == src) {
                input->priv->audiosrc = NULL;

                GST_INFO("Audio input is free");
            }
        }
        else {
            if (input->priv->videosrc == src) {
                input->priv->videosrc = NULL;

                GST_INFO("Video input is free");
            }
        }

        if (!input->priv->videosrc
            && !input->priv->audiosrc) {
            g_hash_table_remove(inputs, id);
            if (g_hash_table_size(inputs) == 0) {
                gst_ndi_input_release_inputs();
            }
        }
    }
}

GstCaps*
gst_ndi_input_get_video_caps(GstNdiInput* input) 
{
    GstCaps* caps = NULL;
    GstNdiInputPriv* priv = input->priv;
    gint dest_n = 1, dest_d = 1;

    g_mutex_lock(&priv->lock);
    if (priv->picture_aspect_ratio != 0) {
        double par = (double)(priv->yres * priv->picture_aspect_ratio) / priv->xres;
        gst_util_double_to_fraction(par, &dest_n, &dest_d);
    }

    caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, gst_ndi_util_get_format(priv->FourCC),
        "width", G_TYPE_INT, (int)priv->xres,
        "height", G_TYPE_INT, (int)priv->yres,
        "framerate", GST_TYPE_FRACTION, priv->frame_rate_N, priv->frame_rate_D,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, dest_n, dest_d,
        "interlace-mode", G_TYPE_STRING, gst_ndi_util_get_frame_format(priv->frame_format_type),
        NULL);
    g_mutex_unlock(&priv->lock);

    GST_DEBUG("PAR %.03f", priv->picture_aspect_ratio);

    return caps;
}

GstCaps* 
gst_ndi_input_get_audio_caps(GstNdiInput* input)
{
    GstCaps* caps = NULL;
    GstNdiInputPriv* priv = input->priv;

    g_mutex_lock(&priv->lock);
    guint64 channel_mask = (1ULL << priv->channels) - 1;
    caps = gst_caps_new_simple("audio/x-raw",
        "format", G_TYPE_STRING, "F32LE",
        "channels", G_TYPE_INT, (int)priv->channels,
        "rate", G_TYPE_INT, (int)priv->sample_rate,
        "layout", G_TYPE_STRING, "interleaved",
        "channel-mask", GST_TYPE_BITMASK, (guint64)channel_mask,
        NULL);
    g_mutex_unlock(&priv->lock);

    return caps;
}

GstClockTime gst_ndi_input_get_audio_buffer_duration(GstNdiInput* input) {
    GstNdiInputPriv* priv = input->priv;
    GstClockTime res = gst_util_uint64_scale_int(priv->audio_buffer_size,
            GST_SECOND, priv->sample_rate * sizeof(float) * priv->channels);

    return res;
}

int gst_ndi_input_get_channels(GstNdiInput* input) {
    return input->priv->channels;
}

guint gst_ndi_input_get_audio_buffer_size(GstNdiInput* input) {
    return input->priv->audio_buffer_size;
}

int gst_ndi_input_get_frame_rate_n(GstNdiInput* input) {
    return input->priv->frame_rate_N;
}

int gst_ndi_input_get_frame_rate_d(GstNdiInput* input) {
    return input->priv->frame_rate_D;
}

GstClockTime gst_ndi_input_get_video_buffer_duration(GstNdiInput* input) {
    GstNdiInputPriv* priv = input->priv;
    GstClockTime res = gst_util_uint64_scale(GST_SECOND
        , priv->frame_rate_D
        , priv->frame_rate_N);

    return res;
}

void gst_ndi_input_get_video_buffer(GstNdiInput* input, void* id, guint8** buffer, guint* size) {
    /*GstNdiInputPriv* priv = input->priv;

    g_async_queue_lock(priv->queue);
    gint length = g_async_queue_length_unlocked(priv->queue);
    for (gint i = 0; i < length; ++i) {
        gpointer data = g_async_queue_pop_unlocked(priv->queue);
        g_async_queue_push_unlocked(priv->queue, data);
        if (id == data) {*/
            *buffer = ((NDIlib_video_frame_v2_t*)id)->p_data;
            *size = ((NDIlib_video_frame_v2_t*)id)->line_stride_in_bytes * ((NDIlib_video_frame_v2_t*)id)->yres;
            /*break;
        }
    }
    g_async_queue_unlock(priv->queue);*/
}

void gst_ndi_input_release_video_buffer(GstNdiInput* input, void* id) {
    GstNdiInputPriv* priv = input->priv;
    /*gboolean found = FALSE;

    g_async_queue_lock(priv->queue);
    gint length = g_async_queue_length_unlocked(priv->queue);
    GST_DEBUG("Search %p. Total: %i", id, length);
    for (gint i = 0; i < length; ++i) {
        gpointer data = g_async_queue_pop_unlocked(priv->queue);
        if (id == data) {*/
            NDIlib_recv_free_video_v2(priv->pNDI_recv, (NDIlib_video_frame_v2_t*)id);
            GST_DEBUG("Found %p", id);
            g_free((NDIlib_video_frame_v2_t *)id);
            /*found = true;
            break;
        }
        else {
            g_async_queue_push_unlocked(priv->queue, data);
        }
    }
    g_async_queue_unlock(priv->queue);

    if (!found) {
        GST_DEBUG("_____ NOT Found %p", id);
    }*/
}

static void
gst_ndi_input_stop_capture_thread(GstNdiInput* input) {
    if (input->priv->capture_thread) {
        GThread* capture_thread = g_steal_pointer(&input->priv->capture_thread);
        input->priv->is_capture_terminated = TRUE;

        GST_DEBUG("Stop capture thread");

        g_thread_join(capture_thread);
        input->priv->capture_thread = NULL;
    }
}

static GstNdiInput*
gst_ndi_input_create_input()
{
    GstNdiInput* input = g_new0(GstNdiInput, 1);
    input->priv = g_new0(GstNdiInputPriv, 1);
    g_mutex_init(&input->priv->lock);

    return input;
}

static void
gst_ndi_input_free_input(gpointer data) {
    GstNdiInput* input = (GstNdiInput*)data;

    gst_ndi_input_stop_capture_thread(input);
    g_mutex_clear(&input->priv->lock);
    g_free(input->priv);
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
