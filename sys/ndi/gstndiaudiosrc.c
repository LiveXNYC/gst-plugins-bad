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
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw, format=F32LE, channels=[1, 16], rate={44100, 48000, 96000}, "
        "layout=interleaved")
);

#define gst_ndi_audio_src_parent_class parent_class
G_DEFINE_TYPE(GstNdiAudioSrc, gst_ndi_audio_src, GST_TYPE_AUDIO_SRC);

static void gst_ndi_audio_src_set_property(GObject* object,
    guint property_id, const GValue* value, GParamSpec* pspec);
static void gst_ndi_audio_src_get_property(GObject* object,
    guint property_id, GValue* value, GParamSpec* pspec);
static void gst_ndi_audio_src_finalize(GObject* object);

static GstCaps* gst_ndi_audio_src_get_caps(GstBaseSrc* src, GstCaps* filter);

static gboolean gst_ndi_audio_src_open(GstAudioSrc* asrc);
static gboolean gst_ndi_audio_src_close(GstAudioSrc* asrc);
static gboolean gst_ndi_audio_src_prepare(GstAudioSrc* asrc, GstAudioRingBufferSpec* spec);
static gboolean gst_ndi_audio_src_unprepare(GstAudioSrc* asrc);
static guint gst_ndi_audio_src_read(GstAudioSrc* asrc, gpointer data, guint length, GstClockTime* timestamp);
static guint gst_ndi_audio_src_delay(GstAudioSrc* asrc);
static void gst_ndi_audio_src_reset(GstAudioSrc* asrc);

static void gst_ndi_audio_src_acquire_input(GstNdiAudioSrc* self);
static void gst_ndi_audio_src_release_input(GstNdiAudioSrc* self);

static void
gst_ndi_audio_src_class_init(GstNdiAudioSrcClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass* basesrc_class = GST_BASE_SRC_CLASS(klass);
    GstAudioSrcClass* gstaudiosrc_class = GST_AUDIO_SRC_CLASS(klass);

    gobject_class->set_property = gst_ndi_audio_src_set_property;
    gobject_class->get_property = gst_ndi_audio_src_get_property;
    gobject_class->finalize = gst_ndi_audio_src_finalize;

    basesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_get_caps);

    gstaudiosrc_class->open = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_open);
    gstaudiosrc_class->close = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_close);
    gstaudiosrc_class->read = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_read);
    gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_prepare);
    gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_unprepare);
    gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_delay);
    gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR(gst_ndi_audio_src_reset);
    
    g_object_class_install_property(gobject_class, PROP_DEVICE_PATH,
        g_param_spec_string("device-path", "Device Path",
            "The device path", "",
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
    gst_ndi_device_ref();

    gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
    
    self->adapter = gst_adapter_new();
    self->caps = NULL;
    g_mutex_init(&self->adapter_mutex);
    self->input = NULL;

    gst_pad_use_fixed_caps(GST_BASE_SRC_PAD(self));
}

static void
gst_ndi_audio_src_finalize(GObject* object)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC_CAST(object);
    
    GST_DEBUG_OBJECT(self, "Finalize");

    gst_ndi_audio_src_release_input(self);

    if (self->adapter) {
        gst_adapter_clear(self->adapter);
        g_object_unref(self->adapter);
        self->adapter = NULL;
    }

    if (self->device_path) {
        g_free(self->device_path);
        self->device_path = NULL;
    }

    if (self->caps) {
        gst_caps_unref(self->caps);
        self->caps = NULL;
    }
    g_mutex_clear(&self->adapter_mutex);
    
    gst_ndi_device_unref();

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

static gboolean 
gst_ndi_audio_src_open(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);

   GST_DEBUG_OBJECT(self, "Open");
   
   gst_ndi_audio_src_acquire_input(self);

   return (self->input != NULL);
}

static gboolean 
gst_ndi_audio_src_close(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   GST_DEBUG_OBJECT(self, "Close");

   gst_ndi_audio_src_release_input(self);

   return TRUE;
}

static gboolean 
gst_ndi_audio_src_prepare(GstAudioSrc* asrc, GstAudioRingBufferSpec* spec) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   guint bpf, rate;

   bpf = GST_AUDIO_INFO_BPF (&spec->info);
   rate = GST_AUDIO_INFO_RATE (&spec->info);
   GST_INFO_OBJECT (self, "bpf is %i bytes, rate is %i Hz",  bpf, rate);
   GST_INFO_OBJECT(self, "segsize = %i segtotal = %i", spec->segsize, spec->segtotal);
   GST_INFO_OBJECT(self, "latency_time = %llu buffer_time = %llu", spec->latency_time, spec->buffer_time);
   
   spec->segsize = self->input->audio_buffer_size;
   spec->segtotal = 2;
   /* The device usually takes time = 1-2 segments to start producing buffers */
   spec->seglatency = spec->segtotal + 2;

   /* calculate actual latency and buffer times */
   spec->latency_time = spec->segsize * GST_MSECOND / (rate * bpf);
   spec->buffer_time =
       spec->segtotal * spec->segsize * GST_MSECOND / (rate * bpf);

   GST_INFO_OBJECT(self, "segsize = %i segtotal = %i latency_time = %llu buffer_time = %llu"
       , spec->segsize, spec->segtotal, spec->latency_time, spec->buffer_time);

   return TRUE;
}

static gboolean 
gst_ndi_audio_src_unprepare(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   GST_DEBUG_OBJECT(self, "4");
   return TRUE;
}

static guint 
gst_ndi_audio_src_read(GstAudioSrc* asrc, gpointer data, guint length, GstClockTime* timestamp) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);

   g_mutex_lock(&self->adapter_mutex);
   gsize size = gst_adapter_available(self->adapter);
   g_mutex_unlock(&self->adapter_mutex);
   guint wanted = MIN(length, size);
   if (wanted > 0) {
       GST_DEBUG_OBJECT(self, "%u %lu", length, size);
       g_mutex_lock(&self->adapter_mutex);
       const guint8* ndiData = gst_adapter_map(self->adapter, wanted);
       memcpy(data, ndiData, wanted);
       gst_adapter_unmap(self->adapter);
       gst_adapter_flush(self->adapter, wanted);
       g_mutex_unlock(&self->adapter_mutex);
   }

   return wanted;
}

static guint 
gst_ndi_audio_src_delay(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   
   g_mutex_lock(&self->adapter_mutex);
   gsize size = gst_adapter_available(self->adapter);
   g_mutex_unlock(&self->adapter_mutex);
   
   GST_DEBUG_OBJECT(self, "%lu", size);

   return size/8;
}

static void 
gst_ndi_audio_src_reset(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   GST_DEBUG_OBJECT(self, "7");
}

static GstCaps*
gst_ndi_audio_src_get_input_caps(GstNdiAudioSrc* self) {
    if (self->input == NULL) {
        return NULL;
    }
    GstCaps* caps = gst_caps_new_simple("audio/x-raw",
        "format", G_TYPE_STRING, "F32LE",
        "channels", G_TYPE_INT, (int)self->input->channels,
        "rate", G_TYPE_INT, (int)self->input->sample_rate,
        "layout", G_TYPE_STRING, "interleaved",
        NULL);

    guint64 channel_mask = (1ULL << self->input->channels) - 1;
    gst_caps_set_simple(caps, "channel-mask", GST_TYPE_BITMASK, (guint64)channel_mask, NULL);

    return caps;
}

static void
gst_ndi_audio_src_got_frame(GstElement* ndi_device, gint8* buffer, guint size, guint stride) {
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(ndi_device);

    GST_DEBUG_OBJECT(self, "Got frame %u", size);
    if (self->caps == NULL) {
        g_mutex_lock(&self->caps_mutex);
        self->caps = gst_ndi_audio_src_get_input_caps(self);
        g_cond_signal(&self->caps_cond);
        g_mutex_unlock(&self->caps_mutex);

        GST_DEBUG_OBJECT(self, "caps %" GST_PTR_FORMAT, self->caps);
    }

    GstBuffer* tmp = gst_buffer_new_allocate(NULL, size, NULL);
    gsize bufferOffset = 0;
    int frameOffset = 0;
    gint8* frame = (gint8*)buffer;
    gint8* src = NULL;
    guint channel_counter = 0;
    guint channels = self->input->channels;
    for (int i = 0; i < size / sizeof(float); ++i) {
        src = frame + frameOffset + stride * channel_counter;
        ++channel_counter;
        if (channel_counter == channels) {
            frameOffset += sizeof(float);
            channel_counter = 0;
        }

        gst_buffer_fill(tmp, bufferOffset, src, sizeof(float));
        bufferOffset += sizeof(float);
    }
    g_mutex_lock(&self->adapter_mutex);
    gst_adapter_push(self->adapter, tmp);
    g_mutex_unlock(&self->adapter_mutex);
}

static void gst_ndi_audio_src_acquire_input(GstNdiAudioSrc* self) {
    if (self->input == NULL) {
        GST_DEBUG_OBJECT(self, "Acquire Input");
        self->input = gst_ndi_device_acquire_input(self->device_path, GST_ELEMENT(self), TRUE);
        if (self->input) {
            self->input->got_audio_frame = gst_ndi_audio_src_got_frame;
        }
    }
}

static void gst_ndi_audio_src_release_input(GstNdiAudioSrc* self) {
    if (self->input != NULL) {
        GST_DEBUG_OBJECT(self, "Release Input");
        self->input->got_audio_frame = NULL;
        gst_ndi_device_release_input(self->device_path, GST_ELEMENT(self), TRUE);
        self->input = NULL;
    }
}
