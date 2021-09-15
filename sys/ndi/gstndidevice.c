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
static GMutex ref_mutex;
static guint ref_counter = 0;

static void gst_ndi_device_get_property(GObject* object,
    guint prop_id, GValue* value, GParamSpec* pspec);
static void gst_ndi_device_set_property(GObject* object,
    guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_ndi_device_finalize(GObject* object);
static GstElement* gst_ndi_device_create_element(GstDevice* device,
    const gchar* name);
static void gst_ndi_device_update(const NDIlib_source_t* source, uint32_t no_sources);
static gpointer thread_func(gpointer data);
static guint gst_ndi_device_get_ref_counter(void);
static void gst_ndi_device_remove_device(Device* device);

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
gst_ndi_device_update(const NDIlib_source_t* p_sources, uint32_t no_sources) {
    if (devices == NULL) {
        devices = g_ptr_array_new();
    }

    if (p_sources == NULL || no_sources == 0) {
        while (devices->len > 0) {
            Device* device = (Device*)g_ptr_array_index(devices, 0);
            GST_DEBUG("Release device id = %s", device->id);
            gst_ndi_device_remove_device(device);
        }
        return;
    }

    GST_DEBUG("Updating devices");

    for (guint i = 0; i < devices->len; ) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        gboolean isFind = FALSE;
        for (uint32_t j = 0; j < no_sources; j++) {
            const NDIlib_source_t* source = p_sources + j;
            if (strcmp(device->id, source->p_url_address) == 0) {
                isFind = TRUE;
                break;
            }
        }
        ++i;
        if (!isFind) {
            GST_INFO("Remove device id = %s, name = %s", device->id, device->p_ndi_name);
            gst_ndi_device_remove_device(device);
            i = 0;
        }
    }

    for (uint32_t i = 0; i < no_sources; i++) {
        const NDIlib_source_t* source = p_sources + i;
        
        gboolean isFind = FALSE;
        for (guint j = 0; j < devices->len; ++j) {
            Device* device = (Device*)g_ptr_array_index(devices, j);
            if (strcmp(device->id, source->p_url_address) == 0) {
                isFind = TRUE;

                if (strcmp(device->p_ndi_name, source->p_ndi_name) != 0) {
                    if (device->p_ndi_name) {
                        g_free(device->p_ndi_name);
                    }
                    device->p_ndi_name = g_strdup(source->p_ndi_name);
                }
            }
        }

        if (!isFind) {
            Device* device = g_new0(Device, 1);
            device->id = g_strdup(source->p_url_address);
            device->p_ndi_name = g_strdup(source->p_ndi_name);
            g_mutex_init(&device->input.lock);
            g_ptr_array_add(devices, device);
            GST_INFO("Add device id = %s, name = %s", device->id, device->p_ndi_name);
        }
    }
}

static void
gst_ndi_device_create_finder(void) {
    gst_ndi_finder_create();
}

static void
gst_ndi_update_devices(void) {
    gst_ndi_device_create_finder();

    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = gst_ndi_finder_get_sources(&no_sources);
    gst_ndi_device_update(p_sources, no_sources);
}

static NDIlib_recv_instance_t
gst_ndi_device_create_ndi_receiver(const gchar* url_adress, const gchar* name) {
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
gst_ndi_device_update_video_input(Device* self, NDIlib_video_frame_v2_t* video_frame) {
    self->input.xres = video_frame->xres;
    self->input.yres = video_frame->yres;
    self->input.frame_rate_N = video_frame->frame_rate_N;
    self->input.frame_rate_D = video_frame->frame_rate_D;
    self->input.frame_format_type = video_frame->frame_format_type;
    self->input.FourCC = video_frame->FourCC;
    self->input.stride = video_frame->line_stride_in_bytes;
    if (self->input.got_video_frame) {
        guint size = video_frame->line_stride_in_bytes * video_frame->yres;
        self->input.got_video_frame(self->input.videosrc, (gint8*)video_frame->p_data, size);
    }
}

static void
gst_ndi_device_update_audio_input(Device* self, NDIlib_audio_frame_v2_t* audio_frame) {
    self->input.channels = audio_frame->no_channels;
    self->input.sample_rate = audio_frame->sample_rate;
    self->input.audio_buffer_size = audio_frame->no_samples * sizeof(float) * audio_frame->no_channels;
    int stride = audio_frame->no_channels == 1 ? 0 :audio_frame->channel_stride_in_bytes;
    if (self->input.got_audio_frame) {
        self->input.got_audio_frame(self->input.audiosrc, (gint8*)audio_frame->p_data, self->input.audio_buffer_size
            , stride);
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

    NDIlib_audio_frame_v2_t audio_frame;
    NDIlib_video_frame_v2_t video_frame;
    while (!self->input.is_capture_terminated) {
        NDIlib_frame_type_e res = NDIlib_recv_capture_v2(self->input.pNDI_recv, &video_frame, &audio_frame, NULL, 500);
        if (res == NDIlib_frame_type_video) {
            gst_ndi_device_update_video_input(self, &video_frame);
            NDIlib_recv_free_video_v2(self->input.pNDI_recv, &video_frame);
        }
        else if (res == NDIlib_frame_type_audio) {
            gst_ndi_device_update_audio_input(self, &audio_frame);
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
    
    while (!self->input.is_capture_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;

        NDIlib_framesync_capture_video(self->input.pNDI_recv_sync, &video_frame, NDIlib_frame_format_type_progressive);
        if (video_frame.p_data) {
            gst_ndi_device_update_video_input(self, &video_frame);
            NDIlib_framesync_free_video(self->input.pNDI_recv_sync, &video_frame);
        }

        NDIlib_framesync_capture_audio(self->input.pNDI_recv_sync, &audio_frame, 0, 0, 0);

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

    GST_DEBUG("START NDI CAPTURE THREAD");

    self->input.is_started = TRUE;
    
    gst_ndi_device_capture(self);
    //gst_ndi_device_capture_sync(self);

    self->input.is_started = FALSE;
    
    GST_DEBUG("STOP NDI CAPTURE THREAD");

    return NULL;
}

GstNdiInput *
gst_ndi_device_acquire_input(const char* id, GstElement * src, gboolean is_audio) {
    gst_ndi_update_devices();

    if (!devices) {

        GST_INFO("Acquire input. No devices");
        
        return NULL;
    }

    GST_INFO("Acquire input. Total devices: %d", devices->len);

    gboolean is_error = FALSE;
    for (guint i = 0; i < devices->len; ++i) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        if (strcmp(device->id, id) == 0) {
            if (is_audio) {
                if (device->input.audiosrc == NULL) {
                    device->input.audiosrc = src;
                    device->input.is_audio_enabled = TRUE;

                    GST_INFO("Audio input is acquired");
                }
                else {
                    GST_ERROR("Audio input is busy");

                    is_error = TRUE;
                }
            }
            else {
                if (device->input.videosrc == NULL) {
                    device->input.videosrc = src;
                    device->input.is_video_enabled = TRUE;

                    GST_INFO("Video input is acquired");
                }
                else {
                    GST_ERROR("Video input is busy");
                    
                    is_error = TRUE;
                }
            }

            if (!is_error) {
                if (device->input.capture_thread == NULL) {

                    GST_DEBUG("Start input thread");

                    device->input.is_capture_terminated = FALSE;
                    GError* error = NULL;
                    device->input.capture_thread =
                        g_thread_try_new("GstNdiInputReader", device_capture_thread_func, (gpointer)device, &error);
                }

                GST_DEBUG("ACQUIRE OK");
                
                return &device->input;
            }
        }
    }

    GST_ERROR("Acquire failed");
    
    return NULL;
}

void
gst_ndi_device_src_send_caps_event(GstBaseSrc* src, GstCaps* caps) {
    if (src == NULL) {
        return;
    }

    GstPad* srcpad = GST_BASE_SRC_PAD(src);
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
}

static void 
gst_ndi_device_stop_capture_thread(Device* device) {
    if (device->input.capture_thread) {
        GThread* capture_thread = g_steal_pointer(&device->input.capture_thread);
        device->input.is_capture_terminated = TRUE;

        GST_DEBUG("Stop capture thread");

        g_thread_join(capture_thread);
        device->input.capture_thread = NULL;
    }
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

                    GST_INFO("Audio input is free");
                }
            }
            else {
                if (device->input.videosrc == src) {
                    device->input.videosrc = NULL;
                    device->input.is_video_enabled = FALSE;

                    GST_INFO("Video input is free");
                }
            }

            if (!device->input.is_video_enabled
                && !device->input.is_audio_enabled) {
                gst_ndi_device_stop_capture_thread(device);
            }
        }
    }
}

static void
gst_ndi_device_remove_device(Device* device) {
    if (device == NULL) {
        return;
    }

    gst_ndi_device_stop_capture_thread(device);
    g_free(device->id);
    g_free(device->p_ndi_name);
    g_free(device);
    g_ptr_array_remove(devices, device);
}

static void
gst_ndi_device_release_devices(void) {

    return;

    GST_DEBUG("Release devices");
    gst_ndi_finder_release();

    if (!devices) {
        return;
    }

    while (devices->len > 0) {
        Device* device = (Device*)g_ptr_array_index(devices, 0);
        GST_DEBUG("Release device id = %s", device->id);
        gst_ndi_device_remove_device(device);
    }
    g_ptr_array_unref(devices);
    devices = NULL;
}

GList*
gst_ndi_device_get_devices(void) {
    GList* list = NULL;
    gst_ndi_update_devices();

    // Display all the sources.
    for (guint i = 0; i < devices->len; ++i) {
        Device* device = (Device*)g_ptr_array_index(devices, i);
        GST_DEBUG("id = %s", device->id);
        GstDevice* gstDevice = gst_ndi_device_provider_create_device(device->id, device->p_ndi_name, TRUE);
        list = g_list_append(list, gstDevice);

        gstDevice = gst_ndi_device_provider_create_device(device->id, device->p_ndi_name, FALSE);
        list = g_list_append(list, gstDevice);
    }

    if (gst_ndi_device_get_ref_counter() == 0) {
        gst_ndi_device_release_devices();
    }

    return list;
}

void gst_ndi_device_ref(void) {
    g_mutex_lock(&ref_mutex);
    ++ref_counter;
    GST_DEBUG("Ref counter = %u", ref_counter);
    g_mutex_unlock(&ref_mutex);
}

void gst_ndi_device_unref(void) {
    g_mutex_lock(&ref_mutex);
    --ref_counter;
    GST_DEBUG("Ref counter = %u", ref_counter);
    if (ref_counter == 0) gst_ndi_device_release_devices();
    g_mutex_unlock(&ref_mutex);
}

static guint
gst_ndi_device_get_ref_counter(void) {
    guint rc = 0;
    g_mutex_lock(&ref_mutex);
    rc = ref_counter;
    g_mutex_unlock(&ref_mutex);
    return rc;
}
