#include "gstndifinder.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC(gst_ndi_finder_debug);
#define GST_CAT_DEFAULT gst_ndi_finder_debug

#define gst_ndi_finder_parent_class parent_class
G_DEFINE_TYPE(GstNdiFinder, gst_ndi_finder, GST_TYPE_OBJECT);

struct _GstNdiFinderPrivate
{
    GThread* finder_thread;
    gboolean is_finder_terminated;
    GMutex list_lock;
    NDIlib_find_instance_t pNDI_find;
    gboolean is_finder_started;
    GMutex data_mutex;
    GCond  data_cond;

    GMutex callback_mutex;
    GstDeviceProvider* device_provider;
    Device_Changed callback;
    uint32_t previous_no_sources;
    
    GPtrArray* devices;
};

typedef struct _NdiDevice NdiDevice;
struct _NdiDevice
{
    gchar* id;
    gchar* p_ndi_name;
};

static void
gst_ndi_finder_device_free(gpointer data) {
    NdiDevice* device = (NdiDevice*)data;
    g_free(device->id);
    g_free(device->p_ndi_name);
    g_free(device);
}

static void
gst_ndi_finder_finalize(GObject* object);
static void
gst_ndi_finder_device_update(GstNdiFinder* self, uint32_t timeout);

static void
gst_ndi_finder_class_init(GstNdiFinderClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gst_ndi_finder_finalize;

    GST_DEBUG_CATEGORY_INIT(gst_ndi_finder_debug, "ndifinder",
        0, "debug category for ndifinder element");
}

static void
gst_ndi_finder_init(GstNdiFinder* self)
{
    self->priv = g_new0(GstNdiFinderPrivate, 1);
    g_mutex_init(&self->priv->list_lock);
    g_mutex_init(&self->priv->data_mutex);
    g_mutex_init(&self->priv->callback_mutex);
    g_cond_init(&self->priv->data_cond);
    self->priv->callback = NULL;
    self->priv->device_provider = NULL;
    self->priv->previous_no_sources = 0;
    self->priv->devices = g_ptr_array_new_with_free_func(gst_ndi_finder_device_free);
}

static void
gst_ndi_finder_finalize(GObject* object) {
    GstNdiFinder* self = GST_NDI_FINDER_CAST(object);
    GST_DEBUG_OBJECT(self, "Finalize");

    g_ptr_array_unref(self->priv->devices);
    self->priv->devices = NULL;

    g_free(self->priv);
    self->priv = NULL;
}

static gpointer
thread_func(gpointer data) {
    GstNdiFinder* self = GST_NDI_FINDER_CAST(data);
    GstNdiFinderPrivate* priv = self->priv;
    GST_DEBUG_OBJECT(self, "Finder Thread Started");

    g_mutex_lock(&priv->data_mutex);
    gst_ndi_finder_device_update(self, 500);

    GST_DEBUG_OBJECT(self, "Finder Thread Send Signal");

    priv->is_finder_started = TRUE;
    g_cond_signal(&priv->data_cond);
    g_mutex_unlock(&priv->data_mutex);

    while (!priv->is_finder_terminated) {
        g_mutex_lock(&priv->list_lock);
        gst_ndi_finder_device_update(self, 100);
        g_mutex_unlock(&priv->list_lock);

        g_usleep(100000);
    }

    GST_DEBUG_OBJECT(self, "Finder Thread Finished");

    return NULL;
}

void gst_ndi_finder_start(GstNdiFinder* finder) {
    if (finder == NULL) {
        return;
    }

    GstNdiFinder* self = GST_NDI_FINDER_CAST(finder);
    GstNdiFinderPrivate* priv = self->priv;
    
    if (priv->pNDI_find == NULL) {

        GST_DEBUG("Creating NDI finder");

        priv->is_finder_started = FALSE;
        priv->is_finder_terminated = FALSE;

        NDIlib_find_create_t p_create_settings;
        p_create_settings.show_local_sources = true;
        p_create_settings.p_extra_ips = NULL;
        p_create_settings.p_groups = NULL;
        priv->pNDI_find = NDIlib_find_create_v2(&p_create_settings);

        if (priv->pNDI_find == NULL) {

            GST_ERROR("Creating NDI finder failed");

            return;
        }

        GST_DEBUG("Creating NDI finder thread");

        GError* error = NULL;
        priv->finder_thread =
            g_thread_try_new("GstNdiFinder", thread_func, (gpointer)self, &error);

        GST_DEBUG("Wait signal");

        g_mutex_lock(&priv->data_mutex);
        while (!priv->is_finder_started)
            g_cond_wait(&priv->data_cond, &priv->data_mutex);
        g_mutex_unlock(&priv->data_mutex);

        GST_DEBUG("Signal received");
    }
    else {
        GST_DEBUG("Finder is created already");
    }
}

void gst_ndi_finder_stop(GstNdiFinder* finder) {
    if (finder == NULL) {
        return;
    }
    GstNdiFinder* self = GST_NDI_FINDER_CAST(finder);
    GstNdiFinderPrivate* priv = self->priv;

    if (priv->finder_thread) {
        GST_DEBUG("Stopping NDI finder thread");
        GThread* thread = g_steal_pointer(&priv->finder_thread);
        priv->is_finder_terminated = TRUE;

        g_thread_join(thread);
    }
    GST_DEBUG("Destroy NDI finder");
    // Destroy the NDI finder
    NDIlib_find_destroy(priv->pNDI_find);
    priv->pNDI_find = NULL;
}

const NDIlib_source_t* 
gst_ndi_finder_get_sources(GstNdiFinder* finder, uint32_t* no_sources) {
    *no_sources = 0;
    const NDIlib_source_t* p_sources = NULL;

    if (finder == NULL) {
        return NULL;
    }
    GstNdiFinder* self = GST_NDI_FINDER_CAST(finder);
    GstNdiFinderPrivate* priv = self->priv;

    if (priv->pNDI_find) {
        // Get the updated list of sources
        g_mutex_lock(&priv->list_lock);
        p_sources = NDIlib_find_get_current_sources(priv->pNDI_find, no_sources);
        g_mutex_unlock(&priv->list_lock);
    }

    return p_sources;
}

void 
gst_ndi_finder_set_callback(GstNdiFinder* finder, GstObject* provider, Device_Changed callback) {
    GstNdiFinder* self = GST_NDI_FINDER(finder);

    g_mutex_lock(&self->priv->callback_mutex);
    self->priv->device_provider = GST_DEVICE_PROVIDER(provider);
    self->priv->callback = callback;
    g_mutex_unlock(&self->priv->callback_mutex);
}

static void
gst_ndi_finder_device_changed(GstNdiFinder* self, gboolean isAdd, gchar* id, gchar* name) {
    GST_DEBUG_OBJECT(self, "Device changed");

    g_mutex_lock(&self->priv->callback_mutex);
    if (self->priv->callback && self->priv->device_provider) {
        self->priv->callback(GST_OBJECT(self->priv->device_provider), isAdd, id, name);
    }
    g_mutex_unlock(&self->priv->callback_mutex);
}

static void
gst_ndi_finder_device_update(GstNdiFinder* self, uint32_t timeout) {
    if (NDIlib_find_wait_for_sources(self->priv->pNDI_find, timeout)) {
        uint32_t no_sources = 0;
        const NDIlib_source_t* p_sources = NDIlib_find_get_current_sources(self->priv->pNDI_find, &no_sources);

        if (p_sources == NULL || no_sources == 0) {
            while (self->priv->devices->len > 0) {
                NdiDevice* device = (NdiDevice*)g_ptr_array_index(self->priv->devices, 0);
                GST_DEBUG("Remove device id = %s", device->id);
                gst_ndi_finder_device_changed(self, FALSE, device->id, device->p_ndi_name);
                g_ptr_array_remove_index(self->priv->devices, 0);
            }
            return;
        }

        GST_DEBUG("Updating devices");

        for (guint i = 0; i < self->priv->devices->len; ) {
            NdiDevice* device = (NdiDevice*)g_ptr_array_index(self->priv->devices, i);
            gboolean isFound = FALSE;
            for (uint32_t j = 0; j < no_sources; j++) {
                const NDIlib_source_t* source = p_sources + j;
                if (strcmp(device->id, source->p_url_address) == 0) {
                    isFound = TRUE;
                    break;
                }
            }
            ++i;
            if (!isFound) {
                GST_INFO("Remove device id = %s, name = %s", device->id, device->p_ndi_name);
                gst_ndi_finder_device_changed(self, FALSE, device->id, device->p_ndi_name);
                g_ptr_array_remove(self->priv->devices, device);
                i = 0;
            }
        }

        for (uint32_t i = 0; i < no_sources; i++) {
            const NDIlib_source_t* source = p_sources + i;

            gboolean isFind = FALSE;
            for (guint j = 0; j < self->priv->devices->len; ++j) {
                NdiDevice* device = (NdiDevice*)g_ptr_array_index(self->priv->devices, j);
                if (strcmp(device->id, source->p_url_address) == 0) {
                    isFind = TRUE;

                    if (strcmp(device->p_ndi_name, source->p_ndi_name) != 0) {
                        gst_ndi_finder_device_changed(self, FALSE, device->id, device->p_ndi_name);
                        if (device->p_ndi_name) {
                            g_free(device->p_ndi_name);
                        }
                        device->p_ndi_name = g_strdup(source->p_ndi_name);
                        gst_ndi_finder_device_changed(self, TRUE, device->id, device->p_ndi_name);
                    }
                }
            }

            if (!isFind) {
                NdiDevice* device = g_new0(NdiDevice, 1);
                device->id = g_strdup(source->p_url_address);
                device->p_ndi_name = g_strdup(source->p_ndi_name);
                g_ptr_array_add(self->priv->devices, device);
                GST_INFO("Add device id = %s, name = %s", device->id, device->p_ndi_name);
                gst_ndi_finder_device_changed(self, TRUE, device->id, device->p_ndi_name);
            }
        }
    }
}
