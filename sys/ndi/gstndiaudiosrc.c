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
    gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
    
    self->pNDI_recv = NULL;
    self->adapter = gst_adapter_new();

    gst_pad_use_fixed_caps(GST_BASE_SRC_PAD(self));
}

static void
gst_ndi_audio_src_finalize(GObject* object)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC_CAST(object);

    if (self->pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->pNDI_recv);
        self->pNDI_recv = NULL;
    }

    if (self->adapter) {
        g_object_unref(self->adapter);
        self->adapter = NULL;
    }

    if (self->device_path) {
        g_free(self->device_path);
        self->device_path = NULL;
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

static void
gst_ndi_audio_src_send_caps_event(GstBaseSrc* src, const NDIlib_audio_frame_v2_t* frame) {
    GstPad* srcpad = GST_BASE_SRC_PAD(src);
    GstCaps* caps = gst_util_create_audio_caps(frame);
    GstEvent* event = gst_pad_get_sticky_event(srcpad, GST_EVENT_CAPS, 0);
    if (event) {
        GstCaps* event_caps;
        gst_event_parse_caps(event, &event_caps);
        if (caps != event_caps) {
            gst_event_unref(event);
            event = gst_event_new_caps(caps);
        }
    }
    else {
        event = gst_event_new_caps(caps);
    }
    gst_pad_push_event(srcpad, event);
    gst_caps_unref(caps);
}

static GstCaps*
gst_ndi_audio_src_get_caps(GstBaseSrc* src, GstCaps* filter)
{
    GstNdiAudioSrc* self = GST_NDI_AUDIO_SRC(src);
    GstCaps* caps = NULL;

    if (self->pNDI_recv != NULL) {
        NDIlib_audio_frame_v2_t audio_frame = gst_ndi_util_get_audio_frame(self->pNDI_recv, 5000);
        if (audio_frame.sample_rate > 0) {
            caps = gst_util_create_audio_caps(&audio_frame);
            NDIlib_recv_free_audio_v2(self->pNDI_recv, &audio_frame);
            GST_DEBUG_OBJECT(self, "Audio frame caps %" GST_PTR_FORMAT, caps);
        }
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

   GST_DEBUG_OBJECT(self, "1");

    NDIlib_recv_create_v3_t create;
    create.source_to_connect_to.p_url_address = self->device_path;
    create.source_to_connect_to.p_ndi_name = "";
    create.p_ndi_recv_name = NULL;

    self->pNDI_recv = NDIlib_recv_create_v3(&create);

    if (self->pNDI_recv != NULL) {
        NDIlib_audio_frame_v2_t audio_frame = gst_ndi_util_get_audio_frame(self->pNDI_recv, 5000);
        if (audio_frame.sample_rate > 0) {
            GstCaps* caps = gst_util_create_audio_caps(&audio_frame);
            NDIlib_recv_free_audio_v2(self->pNDI_recv, &audio_frame);
            GST_DEBUG_OBJECT(self, "Audio frame caps %" GST_PTR_FORMAT, caps);
        }
    }
   return TRUE;
}

static gboolean 
gst_ndi_audio_src_close(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   GST_DEBUG_OBJECT(self, "2");
   return TRUE;
}

static gboolean 
gst_ndi_audio_src_prepare(GstAudioSrc* asrc, GstAudioRingBufferSpec* spec) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   guint bpf, rate;

   bpf = GST_AUDIO_INFO_BPF (&spec->info);
   rate = GST_AUDIO_INFO_RATE (&spec->info);
   GST_INFO_OBJECT (self, "bpf is %i bytes, rate is %i Hz",  bpf, rate);

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
   GST_DEBUG_OBJECT(self, "%u %" GST_TIME_FORMAT, length, GST_TIME_ARGS(*timestamp));

   if (self->pNDI_recv != NULL) {
       NDIlib_audio_frame_v2_t audio_frame = gst_ndi_util_get_audio_frame(self->pNDI_recv, 10);
       if (audio_frame.sample_rate > 0) {
           GstBuffer* tmp = gst_buffer_new_allocate(NULL, audio_frame.no_samples * 8, NULL);
           gsize bufferOffset = 0;
           int frameOffset = 0;
           gint8* frame = (gint8*)audio_frame.p_data;
           gint8* src = NULL;
           for (int i = 0; i < audio_frame.no_samples * 2; ++i) {
               if (i & 1) {
                   src = frame + audio_frame.channel_stride_in_bytes + frameOffset;
                   frameOffset += sizeof(float);
               }
               else {
                   src = frame + frameOffset;
               }
               gst_buffer_fill(tmp, bufferOffset, src, sizeof(float));
               bufferOffset += sizeof(float);
           }
           gst_adapter_push(self->adapter, tmp);
           NDIlib_recv_free_audio_v2(self->pNDI_recv, &audio_frame);
       }
   }

   guint wanted = length;
   if (gst_adapter_available(self->adapter) >= wanted) {
       const guint8* ndiData = gst_adapter_map(self->adapter, wanted);
       memcpy(data, ndiData, wanted);
       gst_adapter_unmap(self->adapter);
       gst_adapter_flush(self->adapter, wanted);
       return length;
   }
   return 0;
}

static guint 
gst_ndi_audio_src_delay(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   GST_DEBUG_OBJECT(self, "6");
   return 1;
}

static void 
gst_ndi_audio_src_reset(GstAudioSrc* asrc) {
   GstNdiAudioSrc *self = GST_NDI_AUDIO_SRC (asrc);
   GST_DEBUG_OBJECT(self, "7");
}
