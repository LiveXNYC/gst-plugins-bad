#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndiaudiosrc.h"
#include "gstndiutil.h"

GST_DEBUG_CATEGORY_STATIC(gst_ndi_audio_src_debug);
#define GST_CAT_DEFAULT gst_ndi_audio_src_debug

enum
{
    PROP_0,
    PROP_DEVICE_PATH,
    PROP_DEVICE_NAME,
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(NDI_AUDIO_TEMPLATE_CAPS)
);

static int MAX_QUEUE_LENGTH = 100;

#define gst_ndi_audio_src_parent_class parent_class
G_DEFINE_TYPE(GstNdiAudioSrc, gst_ndi_audio_src, GST_TYPE_PUSH_SRC);

static void gst_ndi_audio_src_set_property(GObject* object,
    guint property_id, const GValue* value, GParamSpec* pspec);
static void gst_ndi_audio_src_get_property(GObject* object,
    guint property_id, GValue* value, GParamSpec* pspec);
static void gst_ndi_audio_src_finalize(GObject* object);

static void gst_ndi_audio_src_acquire_input(GstNdiAudioSrc* self);
static void gst_ndi_audio_src_release_input(GstNdiAudioSrc* self);

static gboolean gst_ndi_audio_src_start(GstBaseSrc* src);
static gboolean gst_ndi_audio_src_stop(GstBaseSrc* src);
static gboolean gst_ndi_audio_src_set_caps(GstBaseSrc* src, GstCaps* caps);
static GstCaps* gst_ndi_audio_src_get_caps(GstBaseSrc* src, GstCaps* filter);
static GstCaps* gst_ndi_audio_src_fixate(GstBaseSrc* src, GstCaps* caps);
static gboolean gst_ndi_audio_src_unlock(GstBaseSrc* src);
static gboolean gst_ndi_audio_src_unlock_stop(GstBaseSrc* src);
static gboolean gst_ndi_audio_src_query(GstBaseSrc* bsrc, GstQuery* query);
static GstFlowReturn gst_ndi_audio_src_create(GstPushSrc* pushsrc, GstBuffer** buffer);
static void gst_ndi_audio_src_get_times(GstBaseSrc* src, GstBuffer* buffer,
    GstClockTime* start, GstClockTime* end);
static GstStateChangeReturn
gst_ndi_audio_src_change_state(GstElement* element,
    GstStateChange transition);

static void
gst_ndi_audio_src_class_init(GstNdiAudioSrcClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass* basesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass* pushsrc_class = GST_PUSH_SRC_CLASS(klass);

    gobject_class->set_property = gst_ndi_audio_src_set_property;
    gobject_class->get_property = gst_ndi_audio_src_get_property;
    gobject_class->finalize = gst_ndi_audio_src_finalize;

    basesrc_class->start = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_start);
    basesrc_class->stop = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_stop);
    //basesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_set_caps);
    basesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_get_caps);
    basesrc_class->fixate = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_fixate);
    basesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_unlock);
    basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_unlock_stop);
    basesrc_class->query = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_query);
    basesrc_class->get_times = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_get_times);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_change_state);

    pushsrc_class->create = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_create);

    g_object_class_install_property(gobject_class, PROP_DEVICE_PATH,
        g_param_spec_string("device-path", "Device Path",
            "The device path", "",
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_DEVICE_NAME,
        g_param_spec_string("device-name", "Device Name",
            "The human-readable device name", "",
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));


    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class, "NDI Audio Source",
        "Audio/Source/Hardware", "Capture audio stream from NDI device",
        "support@teaminua.com");

    GST_DEBUG_CATEGORY_INIT(gst_ndi_audio_src_debug, "ndiaudiosrc",
        0, "debug category for ndiaudiosrc element");
}

static void
gst_ndi_audio_src_init(GstNdiAudioSrc* self)
{
    gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
    
    self->caps = NULL;
    self->input = NULL;
    self->queue = g_async_queue_new();
    self->is_eos = FALSE;

    gst_pad_use_fixed_caps(GST_BASE_SRC_PAD(self));
}

static void
gst_ndi_audio_src_clear_queue(GstNdiAudioSrc* self) {
    if (self->queue) {
        while (g_async_queue_length(self->queue) > 0) {
            GstBuffer* buffer = (GstBuffer*)g_async_queue_pop(self->queue);
            gst_buffer_unref(buffer);
            g_async_queue_remove(self->queue, buffer);
        }
    }
}

static void
gst_ndi_audio_src_finalize(GObject* object) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC_CAST(object);
    
    GST_DEBUG_OBJECT(self, "Finalize");

    gst_ndi_audio_src_release_input(self);

    if (self->device_path) {
        g_free(self->device_path);
        self->device_path = NULL;
    }

    if (self->caps) {
        gst_caps_unref(self->caps);
        self->caps = NULL;
    }

    if (self->queue) {
        gst_ndi_audio_src_clear_queue(self);
        g_async_queue_unref(self->queue);
        self->queue = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

void
gst_ndi_audio_src_set_property(GObject* object, guint property_id,
    const GValue* value, GParamSpec* pspec)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC_CAST(object);

    switch (property_id) {
    case PROP_DEVICE_PATH:
        g_free(self->device_path);
        self->device_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "%s", self->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_free(self->device_name);
        self->device_name = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "%s", self->device_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void
gst_ndi_audio_src_get_property(GObject* object, guint property_id,
    GValue* value, GParamSpec* pspec)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC_CAST(object);

    switch (property_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, self->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_value_set_string(value, self->device_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstCaps*
gst_ndi_audio_src_get_caps(GstBaseSrc* src, GstCaps* filter)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);
    GstCaps* caps = NULL;

    if (self->input) {
        gint64 end_time;
        g_mutex_lock(&self->caps_mutex);
        end_time = g_get_monotonic_time() + 1 * G_TIME_SPAN_SECOND;
        while (self->caps == NULL) {
            if (!g_cond_wait_until(&self->caps_cond, &self->caps_mutex, end_time)) {
                // timeout has passed.
                break;
            }
        }
        g_mutex_unlock(&self->caps_mutex);
    }

    if (self->caps != NULL) {
        caps = gst_caps_copy(self->caps);
    }

    if (!caps)
        caps = gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));

    if (filter) {
        GstCaps* filtered =
            gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
        caps = filtered;
    }
    
    GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, caps);

    return caps;
}

static GstCaps*
gst_ndi_audio_src_get_input_caps(GstNdiAudioSrc* self) {
    GstCaps* caps = NULL;
    g_mutex_lock(&self->input_mutex);
    if (self->input != NULL) {
        caps = gst_ndi_input_get_audio_caps(self->input);
    }
    g_mutex_unlock(&self->input_mutex);

    return caps;
}

static void
gst_ndi_audio_src_got_frame(GstElement* ndi_device, gint8* buffer, guint size, guint stride, gboolean is_caps_changed) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(ndi_device);

    GST_DEBUG_OBJECT(self, "Got frame %u", size);
    if (is_caps_changed || self->caps == NULL) {
        if (self->caps != NULL) {
            GST_DEBUG_OBJECT(self, "caps changed");
            self->is_eos = TRUE;
            gst_caps_unref(self->caps);
        }

        g_mutex_lock(&self->caps_mutex);
        self->caps = gst_ndi_audio_src_get_input_caps(self);
        g_cond_signal(&self->caps_cond);
        g_mutex_unlock(&self->caps_mutex);

        GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, self->caps);
    }

    if (self->is_eos) {
        return;
    }

    g_mutex_lock(&self->input_mutex);

    if (self->input) {
        guint channels = gst_ndi_input_get_channels(self->input);
        GstBuffer* tmp = gst_buffer_new_allocate(NULL, size, NULL);
        GstMapInfo info;
        if (gst_buffer_map(tmp, &info, GST_MAP_WRITE)) {
            int source_offset = 0;
            guint channel_counter = 0;
            float* source = (float*)buffer;
            guint source_size = size / sizeof(float);
            guint source_stride = stride / sizeof(float);
            float* dest = (float*)info.data;
            for (int i = 0; i < source_size; ++i) {
                float* src = source + source_offset + source_stride * channel_counter;
                ++channel_counter;
                if (channel_counter == channels) {
                    ++source_offset;
                    channel_counter = 0;
                }

                *dest = *src;
                ++dest;
            }

            gst_buffer_unmap(tmp, &info);
        }

        guint n;
        n = size / sizeof(float) / channels;
        GST_BUFFER_OFFSET(tmp) = self->n_samples;
        GST_BUFFER_OFFSET_END(tmp) = self->n_samples + n;

        GST_BUFFER_DTS(tmp) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(tmp) = gst_ndi_input_get_audio_buffer_duration(self->input);

        GST_BUFFER_FLAG_UNSET(tmp, GST_BUFFER_FLAG_DISCONT);
        if (self->n_samples == 0) {
            GST_BUFFER_FLAG_SET(tmp, GST_BUFFER_FLAG_DISCONT);
        }

        self->n_samples += n;

        g_async_queue_push(self->queue, tmp);
    }

    g_mutex_unlock(&self->input_mutex);

    gint queue_length = g_async_queue_length(self->queue);
    if (queue_length > MAX_QUEUE_LENGTH) {
        GstBuffer* buffer = (GstBuffer*)g_async_queue_pop(self->queue);
        gst_buffer_unref(buffer);
    }
}

static void gst_ndi_audio_src_acquire_input(GstNdiAudioSrc* self) {
    g_mutex_lock(&self->input_mutex);
    if (self->input == NULL) {
        GST_DEBUG_OBJECT(self, "Acquire Input");
        self->input = gst_ndi_input_acquire(self->device_path, GST_ELEMENT(self), TRUE);
        if (self->input) {
            self->input->got_audio_frame = gst_ndi_audio_src_got_frame;
        }
    }
    g_mutex_unlock(&self->input_mutex);
}

static void gst_ndi_audio_src_release_input(GstNdiAudioSrc* self) {
    g_mutex_lock(&self->input_mutex);
    if (self->input != NULL) {
        GST_DEBUG_OBJECT(self, "Release Input");
        self->input->got_audio_frame = NULL;
        gst_ndi_input_release(self->device_path, GST_ELEMENT(self), TRUE);
        self->input = NULL;
    }
    g_mutex_unlock(&self->input_mutex);
}

gboolean gst_ndi_audio_src_start(GstBaseSrc* src) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);

    GST_DEBUG_OBJECT(self, "Start");
    self->timestamp_offset = 0;
    self->n_samples = 0;

    gst_ndi_audio_src_acquire_input(self);

    return (self->input != NULL);
}

gboolean gst_ndi_audio_src_stop(GstBaseSrc* src) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);
    GST_DEBUG_OBJECT(self, "Stop");

    gst_ndi_audio_src_release_input(self);

    return TRUE;
}

gboolean gst_ndi_audio_src_set_caps(GstBaseSrc* src, GstCaps* caps) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);

    GST_DEBUG_OBJECT(self, "Set caps %" GST_PTR_FORMAT, caps);

    return TRUE;
}

GstCaps* gst_ndi_audio_src_fixate(GstBaseSrc* src, GstCaps* caps) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);

    GstCaps* fixated_caps;
    GST_DEBUG_OBJECT(self, "fixate caps %" GST_PTR_FORMAT, caps);
    fixated_caps = gst_caps_make_writable(caps);
    fixated_caps = gst_caps_fixate(fixated_caps);

    return fixated_caps;
}

gboolean gst_ndi_audio_src_unlock(GstBaseSrc* src) {
    return TRUE;
}

gboolean gst_ndi_audio_src_unlock_stop(GstBaseSrc* src) {
    return TRUE;
}

gboolean gst_ndi_audio_src_query(GstBaseSrc* bsrc, GstQuery* query) {
    gboolean ret = TRUE;
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(bsrc);

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        g_mutex_lock(&self->input_mutex);
        if (self->input) {
            GstClockTime min, max;
            min = gst_ndi_input_get_audio_buffer_duration(self->input);
            max = 5 * min;
            gst_query_set_latency(query, TRUE, min, max);
            
            GST_DEBUG_OBJECT(self, "min: %"GST_TIME_FORMAT" max: %"GST_TIME_FORMAT, GST_TIME_ARGS(min), GST_TIME_ARGS(max));
            
            ret = TRUE;
        }
        else {
            ret = FALSE;
        }
        g_mutex_unlock(&self->input_mutex);
        break;
    }
    default:
        ret = GST_BASE_SRC_CLASS(parent_class)->query(bsrc, query);
        break;
    }

    return ret;
}

static void gst_ndi_audio_src_get_times(GstBaseSrc* src, GstBuffer* buffer,
    GstClockTime* start, GstClockTime* end)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);

    /* for live sources, sync on the timestamp of the buffer */
    if (gst_base_src_is_live(src)) {
        if (GST_BUFFER_TIMESTAMP_IS_VALID(buffer)) {
            *start = GST_BUFFER_TIMESTAMP(buffer);
            if (GST_BUFFER_DURATION_IS_VALID(buffer)) {
                *end = *start + GST_BUFFER_DURATION(buffer);
            }
            else {
                g_mutex_lock(&self->input_mutex);
                if (self->input) {
                    *end = *start + gst_ndi_input_get_audio_buffer_duration(self->input);
                }
                g_mutex_unlock(&self->input_mutex);
            }
        }
    }
}

GstFlowReturn gst_ndi_audio_src_create(GstPushSrc* pushsrc, GstBuffer** buffer) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(pushsrc);

    if (self->is_eos) {
        GST_DEBUG_OBJECT(self, "Caps was changed. EOS");
        *buffer = NULL;
        return GST_FLOW_EOS;
    }

    GstBuffer* buf = g_async_queue_timeout_pop(self->queue, 100000);
    if (!buf) {
        GST_DEBUG_OBJECT(self, "No buffer");
        gsize size = gst_ndi_input_get_audio_buffer_size(self->input);
        buf = gst_buffer_new_allocate(NULL, size, NULL);
        gst_buffer_memset(buf, 0, 0, size);
    }

    GstClock* clock = gst_element_get_clock(GST_ELEMENT(pushsrc));
    GstClockTime t =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(pushsrc)), gst_clock_get_time(clock));
    gst_object_unref(clock);

    GST_BUFFER_PTS(buf) = t + GST_BUFFER_DURATION(buf);

    GST_DEBUG_OBJECT(self, "create for ts %" GST_TIME_FORMAT" %"GST_TIME_FORMAT, GST_TIME_ARGS(t), GST_TIME_ARGS(GST_BUFFER_PTS(buf)));
    
    *buffer = buf;
    
    return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_ndi_audio_src_change_state(GstElement* element, GstStateChange transition) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        gst_ndi_audio_src_clear_queue(self);
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    return ret;
}
