#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstndidevice.h"
#include "gstndiutil.h"
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
    char* id;
    GstNdiOutput output;
    GstNdiInput input;
};
static GPtrArray * devices = NULL;

static GThread* finder_thread = NULL;
static gboolean is_finder_terminated = FALSE;
static GMutex list_lock;
static GCond  list_cond;
static NDIlib_find_instance_t pNDI_find;
static gboolean is_finder_started = FALSE;
static GMutex data_mutex;
static GCond  data_cond;

static void gst_ndi_device_get_property(GObject* object,
    guint prop_id, GValue* value, GParamSpec* pspec);
static void gst_ndi_device_set_property(GObject* object,
    guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_ndi_device_finalize(GObject* object);
static GstElement* gst_ndi_device_create_element(GstDevice* device,
    const gchar* name);
static void gst_ndi_device_update(const char* id);
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
    GstElement* elem = NULL;

    if (self->isVideo) {
        elem = gst_element_factory_make("ndivideosrc", name);
    }
    else {
        elem = gst_element_factory_make("ndiaudiosrc", name);
    }

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


static gboolean
 gst_ndi_device_compare(gconstpointer a, gconstpointer b) {
    return strcmp(((Device*)a)->id, ((Device*)b)->id) == 0;
}

static void
gst_ndi_device_update(const char* id) {
    if (!devices) {
        devices = g_ptr_array_new();
    }

    Device* device = g_new0(Device, 1);
    device->id = g_strdup(id);

    guint index = 0;
    if (g_ptr_array_find_with_equal_func(devices, device, gst_ndi_device_compare, &index)) {
        g_free(device->id);
        g_free(device);
    }
    else {
        g_mutex_init(&device->input.lock);
        g_ptr_array_add(devices, device);
    }
}

static void
gst_ndi_device_create_finder() {
    if (!pNDI_find) {
        GST_DEBUG("Creating Finder");
        is_finder_started = FALSE;
        is_finder_terminated = FALSE;

        NDIlib_find_create_t p_create_settings;
        p_create_settings.show_local_sources = true;
        p_create_settings.p_extra_ips = NULL;
        p_create_settings.p_groups = NULL;
        pNDI_find = NDIlib_find_create_v2(&p_create_settings);

        if (!pNDI_find) {
            GST_DEBUG("Creating Finder FAILED");
            return;
        }

        GST_DEBUG("Creating Finder Thread");
        GError* error = NULL;
        finder_thread =
            g_thread_try_new("GstNdiFinder", thread_func, (gpointer)NULL, &error);

        GST_DEBUG("Wait Signal");
        g_mutex_lock(&data_mutex);
        while (!is_finder_started)
            g_cond_wait(&data_cond, &data_mutex);
        g_mutex_unlock(&data_mutex);
        GST_DEBUG("Signal Received");
    }
    else {
        GST_DEBUG("Finder is created already");
    }
}

static void
gst_decklink_update_devices(void) {
    gst_ndi_device_create_finder();
    if (pNDI_find) {
        GST_DEBUG("Upating devices");
        // Get the updated list of sources
        uint32_t no_sources = 0;
        g_mutex_lock(&list_lock);
        const NDIlib_source_t* p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
        g_mutex_unlock(&list_lock);

        // Display all the sources.
        for (uint32_t i = 0; i < no_sources; i++) {
            gst_ndi_device_update(p_sources[i].p_url_address);
        }
    }
}

static gpointer
 read_thread_func(gpointer data) {
    Device* self = (Device*)data;

    GST_DEBUG("START NDI READ THREAD");

    if (self->input.pNDI_recv == NULL) {
        GST_DEBUG("Create NDI receiver");
        NDIlib_recv_create_v3_t create;
        create.source_to_connect_to.p_url_address = self->id;
        create.source_to_connect_to.p_ndi_name = "";
        create.color_format = NDIlib_recv_color_format_UYVY_BGRA;
        create.bandwidth = NDIlib_recv_bandwidth_highest;
        create.allow_video_fields = FALSE;
        create.p_ndi_recv_name = NULL;
        self->input.pNDI_recv = NDIlib_recv_create_v3(&create);
    }

    self->input.is_started = TRUE;

    while (!self->input.is_read_terminated) {
        NDIlib_audio_frame_v2_t audio_frame;
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_frame_type_e res = NDIlib_recv_capture_v2(self->input.pNDI_recv, &video_frame, &audio_frame, NULL, 5000);
        if (res == NDIlib_frame_type_video) {
            self->input.xres = video_frame.xres;
            self->input.yres = video_frame.yres;
            self->input.frame_rate_N = video_frame.frame_rate_N;
            self->input.frame_rate_D = video_frame.frame_rate_D;
            self->input.frame_format_type = video_frame.frame_format_type;
            self->input.FourCC = video_frame.FourCC;
            if (self->input.got_video_frame) {
                // TODO: get actual size 
                gsize size = video_frame.xres * video_frame.yres * 2;
                self->input.got_video_frame(self->input.videosrc, (gint8*)video_frame.p_data, size);

            }
            NDIlib_recv_free_video_v2(self->input.pNDI_recv, &video_frame);
        }
        else if (res == NDIlib_frame_type_audio) {
            if (self->input.got_audio_frame) {
                self->input.got_audio_frame(self->input.audiosrc, (gint8*)audio_frame.p_data, audio_frame.no_samples * 8
                    , audio_frame.channel_stride_in_bytes);
            }
            NDIlib_recv_free_audio_v2(self->input.pNDI_recv, &audio_frame);
        }
        else if (res == NDIlib_frame_type_error) {
            GST_DEBUG("NDI receive ERROR");
        }
    }

    GST_DEBUG("STOP NDI READ THREAD");

    self->input.is_started = FALSE;

    if (self->input.pNDI_recv != NULL) {
        NDIlib_recv_destroy(self->input.pNDI_recv);
        self->input.pNDI_recv = NULL;
    }

    return NULL;
}

GstNdiInput *
gst_ndi_acquire_input(const char* id, GstElement * src, gboolean is_audio) {
    gst_decklink_update_devices();

    if (!devices) {
        GST_DEBUG("ACQUIRE. No devices");
        return NULL;
    }

    gboolean is_error = FALSE;
    GST_DEBUG("ACQUIRE. Total devices: %d", devices->len);
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
                        g_thread_try_new("GstNdiInputReader", read_thread_func, (gpointer)device, &error);
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
 gst_ndi_release_input(const char* id, GstElement * src, gboolean is_audio) {
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

static gpointer
thread_func(gpointer data) {
    GST_DEBUG("Finder Thread Started");
    
    g_mutex_lock(&data_mutex);
    NDIlib_find_wait_for_sources(pNDI_find, 1000);
    GST_DEBUG("Finder Thread Send Signal");
    is_finder_started = TRUE;
    g_cond_signal(&data_cond);
    g_mutex_unlock(&data_mutex);

    while (!is_finder_terminated) {
        g_mutex_lock(&list_lock);
        NDIlib_find_wait_for_sources(pNDI_find, 100);
        g_mutex_unlock(&list_lock);
        g_usleep(100000);
    }
    
    GST_DEBUG("Finder Thread Finished");
    
    return NULL;
}

static void
gst_ndi_device_release_finder() {
    if (finder_thread) {
        GThread* thread = g_steal_pointer(&finder_thread);
        is_finder_terminated = TRUE;

        g_thread_join(thread);
    }
    // Destroy the NDI finder
    NDIlib_find_destroy(pNDI_find);
}

GList*
gst_ndi_get_devices(void) {
    GList* list = NULL;

    gst_ndi_device_create_finder();

    if (pNDI_find) {
        // Get the updated list of sources
        uint32_t no_sources = 0;
        g_mutex_lock(&list_lock);
        const NDIlib_source_t* p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
        g_mutex_unlock(&list_lock);

        // Display all the sources.
        for (uint32_t i = 0; i < no_sources; i++) {
            GST_DEBUG("%u. %s\n", i + 1, p_sources[i].p_ndi_name);
            
            GstDevice* device = gst_ndi_device_provider_create_device(p_sources[i].p_url_address, p_sources[i].p_ndi_name, TRUE);
            list = g_list_append(list, device);

            device = gst_ndi_device_provider_create_device(p_sources[i].p_url_address, p_sources[i].p_ndi_name, FALSE);
            list = g_list_append(list, device);
        }
    }
    return list;
}
