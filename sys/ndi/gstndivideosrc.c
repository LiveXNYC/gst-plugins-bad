#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndiaudiosrc.h"
#include "gstndivideosrc.h"
#include "gstndiutil.h"
#include <string.h>

GST_DEBUG_CATEGORY(gst_ndi_video_src_debug);
#define GST_CAT_DEFAULT gst_ndi_video_src_debug

enum
{
    PROP_0,
    PROP_DEVICE_PATH,
    PROP_DEVICE_NAME,
};

static int MAX_QUEUE_LENGTH = 10;

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(NDI_VIDEO_TEMPLATE_CAPS));

static void gst_ndi_video_src_finalize(GObject* object);
static void gst_ndi_video_src_get_property(GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec);
static void gst_ndi_video_src_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec);

static gboolean gst_ndi_video_src_start(GstBaseSrc* src);
static gboolean gst_ndi_video_src_stop(GstBaseSrc* src);
static gboolean gst_ndi_video_src_set_caps(GstBaseSrc* src, GstCaps* caps);
static GstCaps* gst_ndi_video_src_get_caps(GstBaseSrc* src, GstCaps* filter);
static GstCaps* gst_ndi_video_src_fixate(GstBaseSrc* src, GstCaps* caps);
static gboolean gst_ndi_video_src_unlock(GstBaseSrc* src);
static gboolean gst_ndi_video_src_unlock_stop(GstBaseSrc* src);
static gboolean gst_ndi_video_src_query(GstBaseSrc* bsrc, GstQuery* query);

static GstFlowReturn
gst_ndi_video_src_create(GstPushSrc* pushsrc, GstBuffer** buffer);
static void
gst_ndi_video_src_get_times(GstBaseSrc* src, GstBuffer* buffer,
    GstClockTime* start, GstClockTime* end);

static gboolean gst_ndi_video_src_acquire_input(GstNdiVideoSrc* self);
static void gst_ndi_video_src_release_input(GstNdiVideoSrc* self);
static void gst_ndi_video_src_free_last_buffer(GstNdiVideoSrc* self);

#define gst_ndi_video_src_parent_class parent_class
G_DEFINE_TYPE(GstNdiVideoSrc, gst_ndi_video_src, GST_TYPE_PUSH_SRC);

static void
gst_ndi_video_src_class_init(GstNdiVideoSrcClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass* basesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass* pushsrc_class = GST_PUSH_SRC_CLASS(klass);

    gobject_class->finalize = gst_ndi_video_src_finalize;
    gobject_class->get_property = gst_ndi_video_src_get_property;
    gobject_class->set_property = gst_ndi_video_src_set_property;

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

    gst_element_class_set_static_metadata(element_class,
        "NDI Video Source",
        "Source/Video/Hardware",
        "Capture video stream from NDI device",
        "support@teaminua.com");

    gst_element_class_add_static_pad_template(element_class, &src_template);

    basesrc_class->start = GST_DEBUG_FUNCPTR(gst_ndi_video_src_start);
    basesrc_class->stop = GST_DEBUG_FUNCPTR(gst_ndi_video_src_stop);
    //basesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_ndi_video_src_set_caps);
    basesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_ndi_video_src_get_caps);
    basesrc_class->fixate = GST_DEBUG_FUNCPTR(gst_ndi_video_src_fixate);
    basesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_ndi_video_src_unlock);
    basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_ndi_video_src_unlock_stop);
    basesrc_class->query = GST_DEBUG_FUNCPTR(gst_ndi_video_src_query);

    pushsrc_class->create = GST_DEBUG_FUNCPTR(gst_ndi_video_src_create);

    GST_DEBUG_CATEGORY_INIT(gst_ndi_video_src_debug, "ndivideosrc", 0,
        "ndivideosrc");
}

static void
gst_ndi_video_src_init(GstNdiVideoSrc* self)
{
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
    gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
    gst_base_src_set_do_timestamp(GST_BASE_SRC(self), TRUE);

    self->device_path = NULL;
    self->device_name = NULL;
    self->caps = NULL;
    self->queue = g_async_queue_new();
    self->last_buffer = NULL;
    self->is_eos = FALSE;
    self->buffer_duration = GST_MSECOND;
}

static void
gst_ndi_video_src_finalize(GObject* object)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(object);

    GST_DEBUG_OBJECT(self, "Finalize");
    
    gst_ndi_video_src_release_input(self);

    if (self->device_name) {
        g_free(self->device_name);
        self->device_name = NULL;
    }
    if (self->device_path) {
        g_free(self->device_path);
        self->device_path = NULL;
    }

    if (self->queue) {
        while (g_async_queue_length(self->queue) > 0) {
            GstBuffer* buffer = (GstBuffer*)g_async_queue_pop(self->queue);
            gst_buffer_unref(buffer);
        }
        g_async_queue_unref(self->queue);
        self->queue = NULL;
    }

    gst_ndi_video_src_free_last_buffer(self);

    if (self->caps) {
        gst_caps_unref(self->caps);
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_ndi_video_src_get_property(GObject* object, guint prop_id, GValue* value,
    GParamSpec* pspec)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, self->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_value_set_string(value, self->device_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_ndi_video_src_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(object);

    switch (prop_id) {
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
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstCaps*
gst_ndi_video_src_get_input_caps(GstNdiVideoSrc* self) {
    GstCaps* caps = NULL;
    g_mutex_lock(&self->input_mutex);
    if (self->input != NULL) {
        caps = gst_ndi_input_get_video_caps(self->input);
        self->buffer_duration = gst_ndi_input_get_video_buffer_duration(self->input);
    }
    g_mutex_unlock(&self->input_mutex);

    return caps;
}

static gboolean
gst_ndi_video_src_start(GstBaseSrc* src)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);
    
    GST_DEBUG_OBJECT(self, "Start");

    self->timestamp_offset = 0;
    self->n_frames = 0;

    gboolean res = gst_ndi_video_src_acquire_input(self);
    if (res) {
        GstBuffer* buf = g_async_queue_timeout_pop(self->queue, 3000000);
        res = buf != NULL;
        if (res) {
            if (self->caps) {
                gst_caps_unref(self->caps);
            }

            self->caps = gst_ndi_video_src_get_input_caps(self);
            self->last_buffer = buf;
            gst_buffer_ref(self->last_buffer);
        }
        else {
            gst_ndi_video_src_release_input(self);
        }
    }

    GST_DEBUG_OBJECT(self, "Start %s", res ? "succed" : "failed");

    return res;
}

static gboolean
gst_ndi_video_src_stop(GstBaseSrc* src)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);

    GST_DEBUG_OBJECT(self, "Stop");

    gst_ndi_video_src_release_input(self);

    return TRUE;
}

static gboolean
gst_ndi_video_src_set_caps(GstBaseSrc* src, GstCaps* caps)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);

    GST_DEBUG_OBJECT(self, "Set caps %" GST_PTR_FORMAT, caps);

    return TRUE;
}

static GstCaps*
gst_ndi_video_src_get_caps(GstBaseSrc* src, GstCaps* filter)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);
    GstCaps* caps = NULL;

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
gst_ndi_video_src_fixate(GstBaseSrc* src, GstCaps* caps) {
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);

    if (self->input == NULL) {
        return caps;
    }

    GstStructure* structure;
    GstCaps* fixated_caps;
    guint i;

    GST_DEBUG_OBJECT(self, "fixate caps %" GST_PTR_FORMAT, caps);

    fixated_caps = gst_caps_make_writable(caps);

    for (i = 0; i < gst_caps_get_size(fixated_caps); ++i) {
        structure = gst_caps_get_structure(fixated_caps, i);
        gst_structure_fixate_field_nearest_int(structure, "width", G_MAXINT);
        gst_structure_fixate_field_nearest_int(structure, "height", G_MAXINT);
        gst_structure_fixate_field_nearest_fraction(structure, "framerate",
            gst_ndi_input_get_frame_rate_n(self->input), gst_ndi_input_get_frame_rate_d(self->input));
    }

    fixated_caps = gst_caps_fixate(fixated_caps);

    return fixated_caps;
}

static gboolean
gst_ndi_video_src_unlock(GstBaseSrc* src) {
    //GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);

    return TRUE;
}

static gboolean
gst_ndi_video_src_unlock_stop(GstBaseSrc* src) {
    //GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);

    return TRUE;
}

static void
gst_ndi_video_src_get_times(GstBaseSrc* src, GstBuffer* buffer,
    GstClockTime* start, GstClockTime* end)
{
    /* for live sources, sync on the timestamp of the buffer */
    if (gst_base_src_is_live(src)) {
        GstClockTime timestamp = GST_BUFFER_PTS(buffer);

        if (GST_CLOCK_TIME_IS_VALID(timestamp)) {
            /* get duration to calculate end time */
            GstClockTime duration = GST_BUFFER_DURATION(buffer);

            if (GST_CLOCK_TIME_IS_VALID(duration)) {
                *end = timestamp + duration;
            }
            *start = timestamp;
        }
    }
    else {
        *start = GST_CLOCK_TIME_NONE;
        *end = GST_CLOCK_TIME_NONE;
    }
}

static GstFlowReturn
gst_ndi_video_src_create(GstPushSrc* pushsrc, GstBuffer** buffer)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(pushsrc);

    if (self->is_eos) {
        GST_DEBUG_OBJECT(self, "Caps was changed. EOS");
        *buffer = NULL;
        return GST_FLOW_EOS;
    }

    guint64 us_timeout = GST_TIME_AS_USECONDS(self->buffer_duration);
    GstBuffer* buf = g_async_queue_timeout_pop(self->queue, us_timeout);
    if (buf) {
        //GST_DEBUG_OBJECT(self, "Got a buffer. Total: %i", g_async_queue_length(self->queue));
        gst_ndi_video_src_free_last_buffer(self);
        
        self->last_buffer = buf;
        gst_buffer_ref(self->last_buffer);
    }
    else {
        GST_DEBUG_OBJECT(self, "No buffer");
        if (self->last_buffer) {
            gst_buffer_ref(self->last_buffer);
            buf = self->last_buffer;
        }
    }

    if (buf) {
        GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;

        GstClock* clock = gst_element_get_clock(GST_ELEMENT(pushsrc));
        GstClockTime t =
            GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(pushsrc)), gst_clock_get_time(clock));
        gst_object_unref(clock);
        
        GST_BUFFER_PTS(buf) = t + self->buffer_duration;
        GST_BUFFER_DURATION(buf) = self->buffer_duration;

        GST_BUFFER_OFFSET(buf) = self->n_frames;
        GST_BUFFER_OFFSET_END(buf) = self->n_frames + 1;

        GST_DEBUG_OBJECT(self, "create for %llu ts %" GST_TIME_FORMAT" %"GST_TIME_FORMAT, self->n_frames, GST_TIME_ARGS(t), GST_TIME_ARGS(GST_BUFFER_PTS(buf)));

        GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DISCONT);
        if (self->n_frames == 0) {
            GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);
        }
        self->n_frames++;

        
        *buffer = buf;
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

typedef struct
{
    GstNdiVideoSrc* self;
    void* id;
} VideoFrameWrapper;

static void
video_frame_free(void* data)
{
    VideoFrameWrapper* obj = (VideoFrameWrapper*)data;
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(obj->self);

    g_mutex_lock(&self->input_mutex);
    if (self->input != NULL) {
        gst_ndi_input_release_video_buffer(self->input, obj->id);
    }
    g_mutex_unlock(&self->input_mutex);
    g_free(obj);
}

static void 
gst_ndi_video_src_got_frame(GstElement* ndi_device, gint8* buffer, guint size, gboolean is_caps_changed, void* id) {
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(ndi_device);

    if (is_caps_changed || self->caps == NULL) {
        if (self->caps != NULL) {
            GST_DEBUG_OBJECT(self, "caps changed");
            self->is_eos = TRUE;
            gst_caps_unref(self->caps);
        }

        self->caps = gst_ndi_video_src_get_input_caps(self);
        GST_DEBUG_OBJECT(self, "new caps %" GST_PTR_FORMAT, self->caps);
    }

    if (self->is_eos) {
        return;
    }

    /*GstBuffer* buf = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_fill(buf, 0, buffer, size);*/
    
    VideoFrameWrapper* obj = (VideoFrameWrapper*)g_malloc0(sizeof(VideoFrameWrapper));
    obj->self = self;
    obj->id = id;
    GstBuffer* buf = gst_buffer_new_wrapped_full((GstMemoryFlags)GST_MEMORY_FLAG_READONLY,
        (gpointer)buffer, size, 0, size,
        obj, (GDestroyNotify)video_frame_free);
    
    g_async_queue_push(self->queue, buf);

    gint queue_length = g_async_queue_length(self->queue);
    if (queue_length > MAX_QUEUE_LENGTH) {
        GstBuffer* buffer = (GstBuffer*)g_async_queue_pop(self->queue);
        gst_buffer_unref(buffer);
    }
    GST_DEBUG_OBJECT(self, "Got a frame. Total: %i", queue_length);
}

static gboolean
gst_ndi_video_src_acquire_input(GstNdiVideoSrc* self) {
    g_mutex_lock(&self->input_mutex);
    if (self->input == NULL) {
        GST_DEBUG_OBJECT(self, "Acquire Input");
        self->input = gst_ndi_input_acquire(self->device_path, GST_ELEMENT(self), FALSE);
        if (self->input) {
            self->input->got_video_frame = gst_ndi_video_src_got_frame;
        }
        else {
            GST_DEBUG_OBJECT(self, "Acquire Input FAILED");
        }
    }
    g_mutex_unlock(&self->input_mutex);

    return (self->input != NULL);
}

static void 
gst_ndi_video_src_release_input(GstNdiVideoSrc* self) {
    g_mutex_lock(&self->input_mutex);
    if (self->input != NULL) {
        GST_DEBUG_OBJECT(self, "Release Input");
        self->input->got_video_frame = NULL;
        gst_ndi_input_release(self->device_path, GST_ELEMENT(self), FALSE);
        self->input = NULL;
    }
    g_mutex_unlock(&self->input_mutex);
}

static gboolean 
gst_ndi_video_src_query(GstBaseSrc* bsrc, GstQuery* query) {
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(bsrc);
    gboolean ret = TRUE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        g_mutex_lock(&self->input_mutex);
        if (self->input) {
            GstClockTime min, max;

            min = gst_util_uint64_scale_ceil(GST_SECOND
                , gst_ndi_input_get_frame_rate_d(self->input)
                , gst_ndi_input_get_frame_rate_n(self->input));
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

static void
gst_ndi_video_src_free_last_buffer(GstNdiVideoSrc* self) {
    if (self->last_buffer) {
        gst_buffer_unref(self->last_buffer);
        self->last_buffer = NULL;
    }
}
