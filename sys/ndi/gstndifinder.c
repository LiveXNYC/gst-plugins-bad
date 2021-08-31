#include "gstndifinder.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(gst_ndi_debug);
#define GST_CAT_DEFAULT gst_ndi_debug

typedef struct _Finder Finder;
struct _Finder
{
    GThread* finder_thread;
    gboolean is_finder_terminated;
    GMutex list_lock;
    NDIlib_find_instance_t pNDI_find;
    gboolean is_finder_started;
    GMutex data_mutex;
    GCond  data_cond;
};

static Finder* finder = NULL;

static gpointer
thread_func(gpointer data) {

    GST_DEBUG("Finder Thread Started");

    g_mutex_lock(&finder->data_mutex);
    NDIlib_find_wait_for_sources(finder->pNDI_find, 1000);

    GST_DEBUG("Finder Thread Send Signal");

    finder->is_finder_started = TRUE;
    g_cond_signal(&finder->data_cond);
    g_mutex_unlock(&finder->data_mutex);

    while (!finder->is_finder_terminated) {
        g_mutex_lock(&finder->list_lock);
        NDIlib_find_wait_for_sources(finder->pNDI_find, 100);
        g_mutex_unlock(&finder->list_lock);
        g_usleep(100000);
    }

    GST_DEBUG("Finder Thread Finished");

    return NULL;
}

void gst_ndi_finder_create() {
    if (finder == NULL) {
        finder = g_new0(Finder, 1);
        g_mutex_init(&finder->list_lock);
        g_mutex_init(&finder->data_mutex);
        g_cond_init(&finder->data_cond);
    }

    if (finder->pNDI_find == NULL) {

        GST_DEBUG("Creating Finder");

        finder->is_finder_started = FALSE;
        finder->is_finder_terminated = FALSE;

        NDIlib_find_create_t p_create_settings;
        p_create_settings.show_local_sources = true;
        p_create_settings.p_extra_ips = NULL;
        p_create_settings.p_groups = NULL;
        finder->pNDI_find = NDIlib_find_create_v2(&p_create_settings);

        if (finder->pNDI_find == NULL) {

            GST_DEBUG("Creating Finder FAILED");

            return;
        }

        GST_DEBUG("Creating Finder Thread");

        GError* error = NULL;
        finder->finder_thread =
            g_thread_try_new("GstNdiFinder", thread_func, (gpointer)NULL, &error);

        GST_DEBUG("Wait Signal");

        g_mutex_lock(&finder->data_mutex);
        while (!finder->is_finder_started)
            g_cond_wait(&finder->data_cond, &finder->data_mutex);
        g_mutex_unlock(&finder->data_mutex);

        GST_DEBUG("Signal Received");
    }
    else {
        GST_DEBUG("Finder is created already");
    }
}

void gst_ndi_finder_release() {
    if (finder == NULL) {
        return;
    }

    if (finder->finder_thread) {
        GThread* thread = g_steal_pointer(&finder->finder_thread);
        finder->is_finder_terminated = TRUE;

        g_thread_join(thread);
    }
    // Destroy the NDI finder
    NDIlib_find_destroy(finder->pNDI_find);
    
    g_free(finder);
    finder = NULL;
}

const NDIlib_source_t* gst_ndi_finder_get_sources(uint32_t* no_sources) {
    *no_sources = 0;
    const NDIlib_source_t* p_sources = NULL;

    if (finder == NULL) {
        return NULL;
    }

    if (finder->pNDI_find) {
        // Get the updated list of sources
        g_mutex_lock(&finder->list_lock);
        p_sources = NDIlib_find_get_current_sources(finder->pNDI_find, no_sources);
        g_mutex_unlock(&finder->list_lock);
    }

    return p_sources;
}
