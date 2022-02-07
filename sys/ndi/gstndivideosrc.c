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

typedef struct
{
    GstBuffer* buffer;
    void* id;
} VideoBufferWrapper;

struct _GstNdiVideoSrcPriv
{
    GstNdiInput* input;
    GMutex input_mutex;
    gchar* device_path;
    gchar* device_name;
    GstCaps* caps;
    VideoBufferWrapper* last_buffer_wrapper;

    GAsyncQueue* queue;
    guint64 n_frames;
    GstClockTime timestamp_offset;
    gboolean is_eos;
    guint64 buffer_duration;
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

    self->priv = g_new0(GstNdiVideoSrcPriv, 1);

    self->priv->device_path = NULL;
    self->priv->device_name = NULL;
    self->priv->caps = NULL;
    self->priv->queue = g_async_queue_new();
    self->priv->is_eos = FALSE;
    self->priv->buffer_duration = GST_MSECOND;
}

static void
gst_ndi_video_src_finalize(GObject* object)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(object);

    GST_DEBUG_OBJECT(self, "Finalize");
    
    gst_ndi_video_src_release_input(self);

    if (self->priv->device_name) {
        g_free(self->priv->device_name);
        self->priv->device_name = NULL;
    }
    if (self->priv->device_path) {
        g_free(self->priv->device_path);
        self->priv->device_path = NULL;
    }

    if (self->priv->caps) {
        gst_caps_unref(self->priv->caps);
    }

    g_free(self->priv);
    self->priv = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_ndi_video_src_get_property(GObject* object, guint prop_id, GValue* value,
    GParamSpec* pspec)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, self->priv->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_value_set_string(value, self->priv->device_name);
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
        g_free(self->priv->device_path);
        self->priv->device_path = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "%s", self->priv->device_path);
        break;
    case PROP_DEVICE_NAME:
        g_free(self->priv->device_name);
        self->priv->device_name = g_value_dup_string(value);
        GST_DEBUG_OBJECT(self, "%s", self->priv->device_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstCaps*
gst_ndi_video_src_get_input_caps(GstNdiVideoSrc* self) {
    GstCaps* caps = NULL;
    g_mutex_lock(&self->priv->input_mutex);
    if (self->priv->input != NULL) {
        caps = gst_ndi_input_get_video_caps(self->priv->input);
        self->priv->buffer_duration = gst_ndi_input_get_video_buffer_duration(self->priv->input);
    }
    g_mutex_unlock(&self->priv->input_mutex);

    return caps;
}

static gboolean
gst_ndi_video_src_start(GstBaseSrc* src)
{
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(src);
    
    GST_DEBUG_OBJECT(self, "Start");

    self->priv->timestamp_offset = 0;
    self->priv->n_frames = 0;

    gboolean res = gst_ndi_video_src_acquire_input(self);
    if (res) {
        VideoBufferWrapper* bufferWrapper = g_async_queue_timeout_pop(self->priv->queue, 3000000);
        res = bufferWrapper != NULL;
        if (res) {
            if (self->priv->caps) {
                gst_caps_unref(self->priv->caps);
            }

            self->priv->caps = gst_ndi_video_src_get_input_caps(self);
            self->priv->last_buffer_wrapper = bufferWrapper;
        }
        else {
            gst_ndi_video_src_release_input(self);
        }
    }

    GST_DEBUG_OBJECT(self, "Start %s", res ? "succeed" : "failed");

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

    if (self->priv->caps != NULL) {
        caps = gst_caps_copy(self->priv->caps);
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

    if (self->priv->input == NULL) {
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
            gst_ndi_input_get_frame_rate_n(self->priv->input), gst_ndi_input_get_frame_rate_d(self->priv->input));
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
    GstNdiVideoSrcPriv* priv = self->priv;

    if (priv->is_eos) {
        GST_DEBUG_OBJECT(self, "Caps was changed. EOS");
        *buffer = NULL;
        return GST_FLOW_EOS;
    }

    GstBuffer* buf = NULL;
    guint64 us_timeout = GST_TIME_AS_USECONDS(priv->buffer_duration);
    VideoBufferWrapper* bufferWrapper = g_async_queue_timeout_pop(priv->queue, us_timeout);
    if (bufferWrapper) {
        //GST_DEBUG_OBJECT(self, "Got a buffer. Total: %i", g_async_queue_length(self->queue));
        buf = bufferWrapper->buffer;
        if (priv->last_buffer_wrapper) {
            gst_ndi_input_release_video_buffer(priv->input, priv->last_buffer_wrapper->id);
            g_free(priv->last_buffer_wrapper);
        }
        priv->last_buffer_wrapper = bufferWrapper;
    }
    else {
        guint8* buffer = NULL;
        guint size = 0;
        if (priv->last_buffer_wrapper) {
            gst_ndi_input_get_video_buffer(priv->input, priv->last_buffer_wrapper->id, &buffer, &size);
        }
        GST_DEBUG_OBJECT(self, "No buffer %u", size);
        buf = gst_buffer_new_allocate(NULL, size, NULL);
        gst_buffer_fill(buf, 0, buffer, size);
    }

    if (buf) {
        GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;

        GstClock* clock = gst_element_get_clock(GST_ELEMENT(pushsrc));
        GstClockTime t =
            GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(pushsrc)), gst_clock_get_time(clock));
        gst_object_unref(clock);
        
        GST_BUFFER_PTS(buf) = t + priv->buffer_duration;
        GST_BUFFER_DURATION(buf) = priv->buffer_duration;

        GST_BUFFER_OFFSET(buf) = priv->n_frames;
        GST_BUFFER_OFFSET_END(buf) = priv->n_frames + 1;

        GST_DEBUG_OBJECT(self, "create for %llu ts %" GST_TIME_FORMAT" %"GST_TIME_FORMAT, priv->n_frames, GST_TIME_ARGS(t), GST_TIME_ARGS(GST_BUFFER_PTS(buf)));

        GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_DISCONT);
        if (priv->n_frames == 0) {
            GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);
        }
        priv->n_frames++;
        
        *buffer = buf;
        return GST_FLOW_OK;
    }

    GST_DEBUG("_____ NO BUFFER ____");
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

    g_mutex_lock(&self->priv->input_mutex);
    if (self->priv->input != NULL) {
        gst_ndi_input_release_video_buffer(self->priv->input, obj->id);
    }
    g_mutex_unlock(&self->priv->input_mutex);
    g_free(obj);
}

static void 
gst_ndi_video_src_got_frame(GstElement* ndi_device, gint8* buffer, guint size, gboolean is_caps_changed, void* id) {
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(ndi_device);
    GstNdiVideoSrcPriv* priv = self->priv;

    if (is_caps_changed || priv->caps == NULL) {
        if (priv->caps != NULL) {
            GST_DEBUG_OBJECT(self, "caps changed");
            priv->is_eos = TRUE;
            gst_caps_unref(priv->caps);
        }

        priv->caps = gst_ndi_video_src_get_input_caps(self);
        GST_DEBUG_OBJECT(self, "new caps %" GST_PTR_FORMAT, priv->caps);
    }

    if (priv->is_eos) {
        return;
    }

    GstBuffer* buf = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_fill(buf, 0, buffer, size);
    
    /*VideoFrameWrapper* obj = (VideoFrameWrapper*)g_malloc0(sizeof(VideoFrameWrapper));
    obj->self = self;
    obj->id = id;
    GstBuffer* buf = gst_buffer_new_wrapped_full((GstMemoryFlags)GST_MEMORY_FLAG_READONLY,
        (gpointer)buffer, size, 0, size,
        obj, (GDestroyNotify)video_frame_free);*/
    
    VideoBufferWrapper* bufferWrapper = g_new0(VideoBufferWrapper, 1);
    bufferWrapper->buffer = buf;
    bufferWrapper->id = id;
    g_async_queue_push(priv->queue, bufferWrapper);

    gint queue_length = g_async_queue_length(priv->queue);
    if (queue_length > MAX_QUEUE_LENGTH) {
        VideoBufferWrapper* buffer_wrapper = (VideoBufferWrapper*)g_async_queue_pop(priv->queue);
        gst_buffer_unref(buffer_wrapper->buffer);
        gst_ndi_input_release_video_buffer(priv->input, buffer_wrapper->id);
    }
    GST_DEBUG_OBJECT(self, "Got a frame %p. Total: %i", id, queue_length);
}

static gboolean
gst_ndi_video_src_acquire_input(GstNdiVideoSrc* self) {
    GstNdiVideoSrcPriv* priv = self->priv;

    g_mutex_lock(&priv->input_mutex);
    if (priv->input == NULL) {
        GST_DEBUG_OBJECT(self, "Acquire Input");
        priv->input = gst_ndi_input_acquire(priv->device_path, GST_ELEMENT(self), FALSE);
        if (priv->input) {
            priv->input->got_video_frame = gst_ndi_video_src_got_frame;
        }
        else {
            GST_DEBUG_OBJECT(self, "Acquire Input FAILED");
        }
    }
    g_mutex_unlock(&priv->input_mutex);

    return (priv->input != NULL);
}

static void 
gst_ndi_video_src_release_input(GstNdiVideoSrc* self) {
    GstNdiVideoSrcPriv* priv = self->priv;

    g_mutex_lock(&priv->input_mutex);
    if (priv->input != NULL) {
        GST_DEBUG_OBJECT(self, "Release Input");
        priv->input->got_video_frame = NULL;

        if (priv->queue) {
            while (g_async_queue_length(priv->queue) > 0) {
                VideoBufferWrapper* bufferWrapper = (VideoBufferWrapper*)g_async_queue_pop(priv->queue);
                gst_buffer_unref(bufferWrapper->buffer);
                gst_ndi_input_release_video_buffer(priv->input, bufferWrapper->id);
                g_free(bufferWrapper);
            }
            g_async_queue_unref(priv->queue);
            priv->queue = NULL;
        }

        gst_ndi_input_release(priv->device_path, GST_ELEMENT(self), FALSE);
        priv->input = NULL;
    }
    g_mutex_unlock(&priv->input_mutex);
}

static gboolean 
gst_ndi_video_src_query(GstBaseSrc* bsrc, GstQuery* query) {
    GstNdiVideoSrc* self = GST_NDI_VIDEO_SRC(bsrc);
    GstNdiVideoSrcPriv* priv = self->priv;
    gboolean ret = TRUE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        g_mutex_lock(&priv->input_mutex);
        if (priv->input) {
            GstClockTime min, max;

            min = gst_util_uint64_scale_ceil(GST_SECOND
                , gst_ndi_input_get_frame_rate_d(priv->input)
                , gst_ndi_input_get_frame_rate_n(priv->input));
            max = 5 * min;
            gst_query_set_latency(query, TRUE, min, max);

            GST_DEBUG_OBJECT(self, "min: %"GST_TIME_FORMAT" max: %"GST_TIME_FORMAT, GST_TIME_ARGS(min), GST_TIME_ARGS(max));

            ret = TRUE;
        }
        else {
            ret = FALSE;
        }
        g_mutex_unlock(&priv->input_mutex);
        break;
    }
    default:
        ret = GST_BASE_SRC_CLASS(parent_class)->query(bsrc, query);
        break;
    }

    return ret;
}
