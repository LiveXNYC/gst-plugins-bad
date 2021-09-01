#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndidevice.h"
#include "gstndiutil.h"
#include "gstndifinder.h"
#include <ndi/Processing.NDI.Lib.h>

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

enum
{
    PROP_0,
    PROP_DEVICE_PATH,
};

G_DEFINE_TYPE(GstNdiDevice, gst_ndi_device, GST_TYPE_DEVICE);

typedef struct _Device Device;
struct _Device
{
    gchar* id;
    gchar* p_ndi_name;
    GstNdiOutput output;
    GstNdiInput input;
};

static GPtrArray * devices = NULL;

static void gst_ndi_device_get_property(GObject* object,
    guint prop_id, GValue* value, GParamSpec* pspec);
static void gst_ndi_device_set_property(GObject* object,
    guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_ndi_device_finalize(GObject* object);
static GstElement* gst_ndi_device_create_element(GstDevice* device,
    const gchar* name);
static void gst_ndi_device_update(const NDIlib_source_t* source);
static gpointer thread_func(gpointer data);

static void
gst_ndi_device_class_init(GstNdiDeviceClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstDeviceClass* dev_class = GST_DEVICE_CLASS(klass);

    dev_class->create_element = gst_ndi_device_create_element;

    gobject_class->get_property = gst_ndi_device_get_property;
    gobject_class->set_property = gst_ndi_device_set_property;
    gobject_class->finalize = gst_ndi_device_finalize;

    g_object_class_install_property(gobject_class, PROP_DEVICE_PATH,
        g_param_spec_string("device", "Device string ID",
            "Device strId", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS)));
}

static void
gst_ndi_device_init(GstNdiDevice* self)
{

}

static void
gst_ndi_device_finalize(GObject* object)
{
    GstNdiDevice* self = GST_NDI_DEVICE(object);

    g_free(self->device_path);

    G_OBJECT_CLASS(gst_ndi_device_parent_class)->finalize(object);
}

static GstElement*
gst_ndi_device_create_element(GstDevice* device, const gchar* name)
{
    GstNdiDevice* self = GST_NDI_DEVICE(device);

    GstElement* elem = gst_element_factory_make(self->isVideo ? "ndivideosrc" : "ndiaudiosrc", name);
    if (elem) {
        g_object_set(elem, "device-path", self->device_path, NULL);
    }

    return elem;
}

static void
gst_ndi_device_get_property(GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    GstNdiDevice* self = GST_NDI_DEVICE(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, self->device_path);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_ndi_device_set_property(GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstNdiDevice* self = GST_NDI_DEVICE(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        self->device_path = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstDevice*
gst_ndi_device_provider_create_device(const char* id, const char* name, gboolean isVideo) {
    GstStructure* props = gst_structure_new("ndi-proplist",
        "device.api", G_TYPE_STRING, "NDI",
        "device.strid", G_TYPE_STRING, GST_STR_NULL(id),
        "device.friendlyName", G_TYPE_STRING, name, NULL);

    GstCaps* caps = isVideo
        ? gst_util_create_default_video_caps()
        : gst_util_create_default_audio_caps();

    GstDevice* device = g_object_new(GST_TYPE_NDI_DEVICE, "device", id,
        "display-name", name,
        "caps", caps,
        "device-class", isVideo ? "Video/Source" : "Audio/Source",
        "properties", props,
        NULL);
    GST_NDI_DEVICE(device)->isVideo = isVideo;
    if (caps) {
        gst_caps_unref(caps);
    }
    gst_structure_free(props);

    return device;
}

static void
gst_ndi_device_update(const NDIlib_source_t* source) {
    gboolean isFind = FALSE;
    for (guint i = 0; i < devices->len; ++i) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        if (device->id == source->p_url_address) {
            isFind = TRUE;
            break;
        }
    }

    if (!isFind) {
        Device* device = g_new0(Device, 1);
        device->id = g_strdup(source->p_url_address);
        device->p_ndi_name = g_strdup(source->p_ndi_name);
        g_mutex_init(&device->input.lock);
        g_ptr_array_add(devices, device);
    }
}

static void
gst_ndi_device_create_finder() {
    gst_ndi_finder_create();
}

static void
gst_decklink_update_devices(void) {
    gst_ndi_device_create_finder();

    if (!devices) {
        devices = g_ptr_array_new();
    }

    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = gst_ndi_finder_get_sources(&no_sources);
    if (p_sources != NULL) {
        GST_DEBUG("Upating devices");

        // Display all the sources.
        for (uint32_t i = 0; i < no_sources; i++) {
            gst_ndi_device_update(&p_sources[i]);
        }
    }
}

static NDIlib_recv_instance_t
gst_ndi_device_create_ndi_receiver(const gchar* url_adress, const gchar* name) {
    GST_DEBUG("Create NDI receiver");

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
gst_ndi_device_update_video_input(Device* self, NDIlib_video_frame_v2_t* video_frame) {
    self->input.xres = video_frame->xres;
    self->input.yres = video_frame->yres;
    self->input.frame_rate_N = video_frame->frame_rate_N;
    self->input.frame_rate_D = video_frame->frame_rate_D;
    self->input.frame_format_type = video_frame->frame_format_type;
    self->input.FourCC = video_frame->FourCC;
    if (self->input.got_video_frame) {
        // TODO: get actual size 
        gsize size = video_frame->xres * video_frame->yres * 2;
        self->input.got_video_frame(self->input.videosrc, (gint8*)video_frame->p_data, size);
    }
}

static void
gst_ndi_device_update_audio_input(Device* self, NDIlib_audio_frame_v2_t* audio_frame) {
    self->input.channels = audio_frame->no_channels;
    self->input.sample_rate = audio_frame->sample_rate;
    if (self->input.got_audio_frame) {
        self->input.got_audio_frame(self->input.audiosrc, (gint8*)audio_frame->p_data, audio_frame->no_samples * 8
            , audio_frame->channel_stride_in_bytes);
    }
}

static void
gst_ndi_device_capture(Device* self) {
    if (self->input.pNDI_recv == NULL) {
        self->input.pNDI_recv = gst_ndi_device_create_ndi_receiver(self->id, self->p_ndi_name);

        if (self->input.pNDI_recv == NULL) {
            return;
        }
    }

    while (!self->input.is_read_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_frame_type_e res = NDIlib_recv_capture_v2(self->input.pNDI_recv, &video_frame, &audio_frame, NULL, 5000);
        if (res == NDIlib_frame_type_video) {
            gst_ndi_device_update_video_input(self, &video_frame);
            NDIlib_recv_free_video_v2(self->input.pNDI_recv, &video_frame);
        }
        else if (res == NDIlib_frame_type_audio) {
            gst_ndi_device_update_audio_input(self, &audio_frame);
            NDIlib_recv_free_audio_v2(self->input.pNDI_recv, &audio_frame);
        }
        else if (res == NDIlib_frame_type_error) {
            GST_DEBUG("NDI receive ERROR");
        }
    }

    if (self->input.pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->input.pNDI_recv);
        self->input.pNDI_recv = NULL;
    }
}

static void
gst_ndi_device_capture_sync(Device* self) {
    if (self->input.pNDI_recv_sync == NULL) {
        self->input.pNDI_recv = gst_ndi_device_create_ndi_receiver(self->id, self->p_ndi_name);
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
    
    while (!self->input.is_read_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;

        NDIlib_framesync_capture_video(self->input.pNDI_recv_sync, &video_frame, NDIlib_frame_format_type_progressive);
        if (video_frame.p_data) {
            gst_ndi_device_update_video_input(self, &video_frame);
            NDIlib_framesync_free_video(self->input.pNDI_recv_sync, &video_frame);
        }

        NDIlib_framesync_capture_audio(self->input.pNDI_recv_sync, &audio_frame, 48000, 2, 1600);

        if (audio_frame.p_data) {
            gst_ndi_device_update_audio_input(self, &audio_frame);
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
 device_capture_thread_func(gpointer data) {
    Device* self = (Device*)data;

    GST_DEBUG("START NDI READ THREAD");

    self->input.is_started = TRUE;
    
    gst_ndi_device_capture(self);
    //gst_ndi_device_capture_sync(self);

    self->input.is_started = FALSE;
    
    GST_DEBUG("STOP NDI READ THREAD");

    return NULL;
}

GstNdiInput *
gst_ndi_device_acquire_input(const char* id, GstElement * src, gboolean is_audio) {
    gst_decklink_update_devices();

    if (!devices) {

        GST_DEBUG("ACQUIRE. No devices");
        
        return NULL;
    }

    GST_DEBUG("ACQUIRE. Total devices: %d", devices->len);

    gboolean is_error = FALSE;
    for (guint i = 0; i < devices->len; ++i) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        if (strcmp(device->id, id) == 0) {
            if (is_audio) {
                if (device->input.audiosrc == NULL) {
                    device->input.audiosrc = src;
                    device->input.is_audio_enabled = TRUE;

                    GST_DEBUG("Audio Input is acquired");
                }
                else {
                    GST_DEBUG("Audio Input is busy");

                    is_error = TRUE;
                }
            }
            else {
                if (device->input.videosrc == NULL) {
                    device->input.videosrc = src;
                    device->input.is_video_enabled = TRUE;

                    GST_DEBUG("Video Input is acquired");
                }
                else {
                    GST_DEBUG("Video Input is busy");
                    
                    is_error = TRUE;
                }
            }

            if (!is_error) {
                if (device->input.read_thread == NULL) {

                    GST_DEBUG("Start input thread");

                    device->input.is_read_terminated = FALSE;
                    GError* error = NULL;
                    device->input.read_thread =
                        g_thread_try_new("GstNdiInputReader", device_capture_thread_func, (gpointer)device, &error);
                }

                GST_DEBUG("ACQUIRE OK");
                
                return &device->input;
            }
        }
    }

    GST_DEBUG("ACQUIRE FAILED");
    
    return NULL;
}

void
 gst_ndi_device_release_input(const char* id, GstElement * src, gboolean is_audio) {
    for (guint i = 0; i < devices->len; ++i) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        if (strcmp(device->id, id) == 0) {
            if (is_audio) {
                if (device->input.audiosrc == src) {
                    device->input.audiosrc = NULL;
                    device->input.is_audio_enabled = FALSE;

                    GST_DEBUG("Audio Input is free");
                }
            }
            else {
                if (device->input.videosrc == src) {
                    device->input.videosrc = NULL;
                    device->input.is_video_enabled = FALSE;

                    GST_DEBUG("Video Input is free");
                }
            }

            if (device->input.read_thread 
                && !device->input.is_video_enabled
                && !device->input.is_audio_enabled) {
                GThread* read_thread = g_steal_pointer(&device->input.read_thread);
                device->input.is_read_terminated = TRUE;
                
                GST_DEBUG("Stop input thread");
                
                g_thread_join(read_thread);
                device->input.read_thread = NULL;
            }
        }
    }
}

static void
gst_ndi_device_release_finder() {
    gst_ndi_finder_release();
}

GList*
gst_ndi_device_get_devices(void) {
    GList* list = NULL;
    gst_decklink_update_devices();

    // Display all the sources.
    for (guint i = 0; i < devices->len; ++i) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        GST_DEBUG("id = %s", device->id);

        GstDevice* gstDevice = gst_ndi_device_provider_create_device(device->id, device->p_ndi_name, TRUE);
        list = g_list_append(list, gstDevice);

        gstDevice = gst_ndi_device_provider_create_device(device->id, device->p_ndi_name, FALSE);
        list = g_list_append(list, gstDevice);
    }

    return list;
}
