/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include "gstmfsourcereader.h"
#include <string.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_mf_source_object_debug);
#define GST_CAT_DEFAULT gst_mf_source_object_debug

G_END_DECLS

typedef struct _GstMFStreamMediaType
{
  IMFMediaType *media_type;

  /* the stream index of media type */
  guint stream_index;

  /* the media index in the stream index */
  guint media_type_index;

  GstCaps *caps;
} GstMFStreamMediaType;

typedef struct
{
  IMFActivate *handle;
  guint index;
  gchar *name;
  gchar *path;
} GstMFDeviceActivate;

struct _GstMFSourceReader
{
  GstMFSourceObject parent;

  GThread *thread;
  GMutex lock;
  GCond cond;
  GMainContext *context;
  GMainLoop *loop;

  /* protected by lock */
  GQueue *queue;

  IMFMediaSource *source;
  IMFSourceReader *reader;

  GstCaps *supported_caps;
  GList *media_types;
  GstMFStreamMediaType *cur_type;
  GstVideoInfo info;

  gboolean top_down_image;

  gboolean flushing;
};

static void gst_mf_source_reader_constructed (GObject * object);
static void gst_mf_source_reader_finalize (GObject * object);

static gboolean gst_mf_source_reader_start (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_stop  (GstMFSourceObject * object);
static GstFlowReturn gst_mf_source_reader_fill (GstMFSourceObject * object,
    GstBuffer * buffer);
static GstFlowReturn gst_mf_source_reader_create (GstMFSourceObject * object,
    GstBuffer ** buffer);
static gboolean gst_mf_source_reader_unlock (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_unlock_stop (GstMFSourceObject * object);
static GstCaps * gst_mf_source_reader_get_caps (GstMFSourceObject * object);
static gboolean gst_mf_source_reader_set_caps (GstMFSourceObject * object,
    GstCaps * caps);

static gboolean gst_mf_source_reader_open (GstMFSourceReader * object,
    IMFActivate * activate);
static gboolean gst_mf_source_reader_close (GstMFSourceReader * object);
static gpointer gst_mf_source_reader_thread_func (GstMFSourceReader * self);
static gboolean gst_mf_source_enum_device_activate (GstMFSourceReader * self,
    GstMFSourceType source_type, GList ** device_activates);
static void gst_mf_device_activate_free (GstMFDeviceActivate * activate);

#define gst_mf_source_reader_parent_class parent_class
G_DEFINE_TYPE (GstMFSourceReader, gst_mf_source_reader,
    GST_TYPE_MF_SOURCE_OBJECT);

static void
gst_mf_source_reader_class_init (GstMFSourceReaderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstMFSourceObjectClass *source_class = GST_MF_SOURCE_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_mf_source_reader_constructed;
  gobject_class->finalize = gst_mf_source_reader_finalize;

  source_class->start = GST_DEBUG_FUNCPTR (gst_mf_source_reader_start);
  source_class->stop = GST_DEBUG_FUNCPTR (gst_mf_source_reader_stop);
  source_class->fill = GST_DEBUG_FUNCPTR (gst_mf_source_reader_fill);
  source_class->create = GST_DEBUG_FUNCPTR (gst_mf_source_reader_create);
  source_class->unlock = GST_DEBUG_FUNCPTR (gst_mf_source_reader_unlock);
  source_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_mf_source_reader_unlock_stop);
  source_class->get_caps = GST_DEBUG_FUNCPTR (gst_mf_source_reader_get_caps);
  source_class->set_caps = GST_DEBUG_FUNCPTR (gst_mf_source_reader_set_caps);
}

static void
gst_mf_source_reader_init (GstMFSourceReader * self)
{
  self->queue = g_queue_new ();
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);
}

static void
gst_mf_source_reader_constructed (GObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);

  /* Create a new thread to ensure that COM thread can be MTA thread */
  g_mutex_lock (&self->lock);
  self->thread = g_thread_new ("GstMFSourceReader",
      (GThreadFunc) gst_mf_source_reader_thread_func, self);
  while (!g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, &self->lock);
  g_mutex_unlock (&self->lock);
}

static gboolean
gst_mf_enum_media_type_from_source_reader (IMFSourceReader * source_reader,
    GList ** media_types)
{
  gint i, j;
  HRESULT hr;
  GList *list = NULL;
  std::vector<std::string> unhandled_caps;

  g_return_val_if_fail (source_reader != NULL, FALSE);
  g_return_val_if_fail (media_types != NULL, FALSE);

  {
    /* Retrive only the first video stream. non-first video stream might be
     * photo stream which doesn't seem to be working propertly in this implementation.
     *
     * Note: Chromium seems to be using the first video stream
     * https://github.com/chromium/chromium/blob/ccd149af47315e4c6f2fc45d55be1b271f39062c/media/capture/video/win/video_capture_device_factory_win.cc
     */
    i = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    for (j = 0;; j++) {
      ComPtr<IMFMediaType> media_type;

      hr = source_reader->GetNativeMediaType (i, j, &media_type);

      if (SUCCEEDED (hr)) {
        GstMFStreamMediaType *mtype;
        GstCaps *caps = NULL;
        GstStructure *s;
        std::string name;

        caps = gst_mf_media_type_to_caps (media_type.Get ());

        /* unknown format */
        if (!caps)
          continue;

        s = gst_caps_get_structure (caps, 0);
        name = gst_structure_get_name (s);
        if (name != "video/x-raw" && name != "image/jpeg") {
          auto it =
              std::find(unhandled_caps.begin(), unhandled_caps.end(), name);
          if (it == unhandled_caps.end()) {
            GST_FIXME ("Skip not supported format %s", name.c_str());
            unhandled_caps.push_back(name);
          }
          gst_caps_unref (caps);
          continue;
        }

        mtype = g_new0 (GstMFStreamMediaType, 1);

        mtype->media_type = media_type.Detach ();
        mtype->stream_index = i;
        mtype->media_type_index = j;
        mtype->caps = caps;

        GST_DEBUG ("StreamIndex %d, MediaTypeIndex %d, %" GST_PTR_FORMAT,
            i, j, caps);

        list = g_list_prepend (list, mtype);
      } else if (hr == MF_E_NO_MORE_TYPES) {
        /* no more media type in this stream index, try next stream index */
        break;
      } else if (hr == MF_E_INVALIDSTREAMNUMBER) {
        /* no more streams and media types */
        goto done;
      } else {
        /* undefined return */
        goto done;
      }
    }
  }

done:
  list = g_list_reverse (list);
  *media_types = list;

  return ! !list;
}

static void
gst_mf_stream_media_type_free (GstMFStreamMediaType * media_type)
{
  g_return_if_fail (media_type != NULL);

  if (media_type->media_type)
    media_type->media_type->Release ();

  if (media_type->caps)
    gst_caps_unref (media_type->caps);

  g_free (media_type);
}

static gint
compare_caps_func (gconstpointer a, gconstpointer b)
{
  GstMFStreamMediaType *m1, *m2;

  m1 = (GstMFStreamMediaType *) a;
  m2 = (GstMFStreamMediaType *) b;

  return gst_mf_source_object_caps_compare (m1->caps, m2->caps);
}

static gboolean
gst_mf_source_reader_open (GstMFSourceReader * self, IMFActivate * activate)
{
  GList *iter;
  HRESULT hr;
  ComPtr<IMFSourceReader> reader;
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFAttributes> attr;

  hr = activate->ActivateObject (IID_IMFMediaSource, (void **) &source);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFCreateAttributes (&attr, 2);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = attr->SetUINT32 (MF_READWRITE_DISABLE_CONVERTERS, TRUE);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFCreateSourceReaderFromMediaSource (source.Get (),
      attr.Get (), &reader);
  if (!gst_mf_result (hr))
    return FALSE;

  if (!gst_mf_enum_media_type_from_source_reader (reader.Get (),
          &self->media_types)) {
    GST_ERROR_OBJECT (self, "No available media types");
    source->Shutdown ();
    return FALSE;
  }

  self->source = source.Detach ();
  self->reader = reader.Detach ();

  self->media_types = g_list_sort (self->media_types,
      (GCompareFunc) compare_caps_func);

  self->supported_caps = gst_caps_new_empty ();

  for (iter = self->media_types; iter; iter = g_list_next (iter)) {
    GstMFStreamMediaType *mtype = (GstMFStreamMediaType *) iter->data;

    gst_caps_append (self->supported_caps, gst_caps_copy (mtype->caps));
  }

  GST_DEBUG_OBJECT (self, "Available output caps %" GST_PTR_FORMAT,
      self->supported_caps);

  return TRUE;
}

static gboolean
gst_mf_source_reader_close (GstMFSourceReader * self)
{
  gst_clear_caps (&self->supported_caps);

  if (self->media_types) {
    g_list_free_full (self->media_types,
        (GDestroyNotify) gst_mf_stream_media_type_free);
    self->media_types = NULL;
  }

  if (self->reader) {
    self->reader->Release ();
    self->reader = NULL;
  }

  if (self->source) {
    self->source->Shutdown ();
    self->source->Release ();
    self->source = NULL;
  }

  return TRUE;
}

static void
gst_mf_source_reader_finalize (GObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  g_main_loop_quit (self->loop);
  g_thread_join (self->thread);
  g_main_loop_unref (self->loop);
  g_main_context_unref (self->context);

  g_queue_free (self->queue);
  gst_clear_caps (&self->supported_caps);
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_mf_source_reader_start (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  HRESULT hr;
  GstMFStreamMediaType *type;

  if (!self->cur_type) {
    GST_ERROR_OBJECT (self, "MediaType wasn't specified");
    return FALSE;
  }

  type = self->cur_type;
  self->top_down_image = TRUE;

  if (GST_VIDEO_INFO_FORMAT (&self->info) != GST_VIDEO_FORMAT_ENCODED) {
    UINT32 stride;
    INT32 actual_stride = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, 0);

    /* This MF_MT_DEFAULT_STRIDE uses UINT32 type but actual value is
     * INT32, which can be negative in case of RGB image, and negative means
     * its stored as bottom-up manner */
    hr = type->media_type->GetUINT32 (MF_MT_DEFAULT_STRIDE, &stride);
    if (gst_mf_result (hr)) {
      actual_stride = (INT32) stride;
      if (actual_stride < 0) {
        if (!GST_VIDEO_INFO_IS_RGB (&self->info)) {
          GST_ERROR_OBJECT (self,
              "Bottom-up image is allowed only for RGB format");
          return FALSE;
        }

        GST_DEBUG_OBJECT (self,
            "Detected bottom-up image, stride %d", actual_stride);

        self->top_down_image = FALSE;
      }
    } else {
      /* If MF_MT_DEFAULT_STRIDE attribute is not specified, we can use our
       * value */
      type->media_type->SetUINT32 (MF_MT_DEFAULT_STRIDE,
          (UINT32) actual_stride);
    }
    gst_mf_update_video_info_with_stride (&self->info,
        std::abs (actual_stride));
  }

  hr = self->reader->SetStreamSelection (type->stream_index, TRUE);
  if (!gst_mf_result (hr))
    return FALSE;

  hr = self->reader->SetCurrentMediaType (type->stream_index,
      NULL, type->media_type);
  if (!gst_mf_result (hr))
    return FALSE;

  return TRUE;
}

static gboolean
gst_mf_source_reader_stop (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  while (!g_queue_is_empty (self->queue)) {
    IMFMediaBuffer *buffer = (IMFMediaBuffer *) g_queue_pop_head (self->queue);
    buffer->Release ();
  }

  return TRUE;
}

static GstFlowReturn
gst_mf_source_reader_read_sample (GstMFSourceReader * self)
{
  HRESULT hr;
  DWORD count = 0, i;
  DWORD stream_flags = 0;
  GstMFStreamMediaType *type = self->cur_type;
  ComPtr<IMFSample> sample;

  hr = self->reader->ReadSample (type->stream_index, 0, NULL, &stream_flags,
    NULL, &sample);

  if (!gst_mf_result (hr))
    return GST_FLOW_ERROR;

  if ((stream_flags & MF_SOURCE_READERF_ERROR) == MF_SOURCE_READERF_ERROR)
    return GST_FLOW_ERROR;

  if (!sample)
    return GST_FLOW_OK;

  hr = sample->GetBufferCount (&count);
  if (!gst_mf_result (hr) || !count)
    return GST_FLOW_OK;

  for (i = 0; i < count; i++) {
    IMFMediaBuffer *buffer = NULL;

    hr = sample->GetBufferByIndex (i, &buffer);
    if (!gst_mf_result (hr) || !buffer)
      continue;

    g_queue_push_tail (self->queue, buffer);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mf_source_reader_get_media_buffer (GstMFSourceReader * self,
    IMFMediaBuffer ** media_buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  while (g_queue_is_empty (self->queue)) {
    ret = gst_mf_source_reader_read_sample (self);
    if (ret != GST_FLOW_OK)
      return ret;

    g_mutex_lock (&self->lock);
    if (self->flushing) {
      g_mutex_unlock (&self->lock);
      return GST_FLOW_FLUSHING;
    }
    g_mutex_unlock (&self->lock);
  }

  *media_buffer = (IMFMediaBuffer *) g_queue_pop_head (self->queue);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mf_source_reader_fill (GstMFSourceObject * object, GstBuffer * buffer)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  GstFlowReturn ret = GST_FLOW_OK;
  ComPtr<IMFMediaBuffer> media_buffer;
  GstVideoFrame frame;
  BYTE *data;
  gint i, j;
  HRESULT hr;

  ret = gst_mf_source_reader_get_media_buffer (self, &media_buffer);
  if (ret != GST_FLOW_OK)
    return ret;

  hr = media_buffer->Lock (&data, NULL, NULL);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Failed to lock media buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&frame, &self->info, buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    media_buffer->Unlock ();
    return GST_FLOW_ERROR;
  }

  if (!self->top_down_image) {
    guint8 *src, *dst;
    gint src_stride, dst_stride;
    gint width, height;

    /* must be single plane RGB */
    width = GST_VIDEO_INFO_COMP_WIDTH (&self->info, 0)
        * GST_VIDEO_INFO_COMP_PSTRIDE (&self->info, 0);
    height = GST_VIDEO_INFO_HEIGHT (&self->info);

    src_stride = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, 0);
    dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);

    /* This is bottom up image, should copy lines in reverse order */
    src = data + src_stride * (height - 1);
    dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);

    for (j = 0; j < height; j++) {
      memcpy (dst, src, width);
      src -= src_stride;
      dst += dst_stride;
    }
  } else {
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&self->info); i++) {
      guint8 *src, *dst;
      gint src_stride, dst_stride;
      gint width;

      src = data + GST_VIDEO_INFO_PLANE_OFFSET (&self->info, i);
      dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, i);

      src_stride = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, i);
      dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
      width = GST_VIDEO_INFO_COMP_WIDTH (&self->info, i)
          * GST_VIDEO_INFO_COMP_PSTRIDE (&self->info, i);

      for (j = 0; j < GST_VIDEO_INFO_COMP_HEIGHT (&self->info, i); j++) {
        memcpy (dst, src, width);
        src += src_stride;
        dst += dst_stride;
      }
    }
  }

  gst_video_frame_unmap (&frame);
  media_buffer->Unlock ();

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mf_source_reader_create (GstMFSourceObject * object, GstBuffer ** buffer)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  GstFlowReturn ret = GST_FLOW_OK;
  ComPtr<IMFMediaBuffer> media_buffer;
  HRESULT hr;
  BYTE *data;
  DWORD len = 0;
  GstBuffer *buf;
  GstMapInfo info;

  ret = gst_mf_source_reader_get_media_buffer (self, &media_buffer);
  if (ret != GST_FLOW_OK)
    return ret;

  hr = media_buffer->Lock (&data, NULL, &len);
  if (!gst_mf_result (hr) || len == 0) {
    GST_ERROR_OBJECT (self, "Failed to lock media buffer");
    return GST_FLOW_ERROR;
  }

  buf = gst_buffer_new_and_alloc (len);
  if (!buf) {
    GST_ERROR_OBJECT (self, "Cannot allocate buffer");
    media_buffer->Unlock ();
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (buf, &info, GST_MAP_WRITE);
  memcpy (info.data, data, len);
  gst_buffer_unmap (buf, &info);

  media_buffer->Unlock ();

  *buffer = buf;

  return GST_FLOW_OK;
}

static gboolean
gst_mf_source_reader_unlock (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  g_mutex_lock (&self->lock);
  self->flushing = TRUE;
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static gboolean
gst_mf_source_reader_unlock_stop (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  g_mutex_lock (&self->lock);
  self->flushing = FALSE;
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static GstCaps *
gst_mf_source_reader_get_caps (GstMFSourceObject * object)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);

  if (self->supported_caps)
    return gst_caps_ref (self->supported_caps);

  return NULL;
}

static gboolean
gst_mf_source_reader_set_caps (GstMFSourceObject * object, GstCaps * caps)
{
  GstMFSourceReader *self = GST_MF_SOURCE_READER (object);
  GList *iter;
  GstMFStreamMediaType *best_type = NULL;

  for (iter = self->media_types; iter; iter = g_list_next (iter)) {
    GstMFStreamMediaType *minfo = (GstMFStreamMediaType *) iter->data;
    if (gst_caps_can_intersect (minfo->caps, caps)) {
      best_type = minfo;
      break;
    }
  }

  if (!best_type) {
    GST_ERROR_OBJECT (self,
        "Could not determine target media type with given caps %"
        GST_PTR_FORMAT, caps);

    return FALSE;
  }

  self->cur_type = best_type;
  gst_video_info_from_caps (&self->info, best_type->caps);

  return TRUE;
}

static gboolean
gst_mf_source_reader_main_loop_running_cb (GstMFSourceReader * self)
{
  GST_INFO_OBJECT (self, "Main loop running now");

  g_mutex_lock (&self->lock);
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

static gpointer
gst_mf_source_reader_thread_func (GstMFSourceReader * self)
{
  GstMFSourceObject *object = GST_MF_SOURCE_OBJECT (self);
  GSource *source;
  GList *activate_list = NULL;
  GstMFDeviceActivate *target = NULL;
  GList *iter;

  CoInitializeEx (NULL, COINIT_MULTITHREADED);

  g_main_context_push_thread_default (self->context);

  source = g_idle_source_new ();
  g_source_set_callback (source,
      (GSourceFunc) gst_mf_source_reader_main_loop_running_cb, self, NULL);
  g_source_attach (source, self->context);
  g_source_unref (source);

  if (!gst_mf_source_enum_device_activate (self,
          object->source_type, &activate_list)) {
    GST_WARNING_OBJECT (self, "No available video capture device");
    goto run_loop;
  }
#ifndef GST_DISABLE_GST_DEBUG
  for (iter = activate_list; iter; iter = g_list_next (iter)) {
    GstMFDeviceActivate *activate = (GstMFDeviceActivate *) iter->data;

    GST_DEBUG_OBJECT (self, "device %d, name: \"%s\", path: \"%s\"",
        activate->index, GST_STR_NULL (activate->name),
        GST_STR_NULL (activate->path));
  }
#endif

  GST_DEBUG_OBJECT (self,
      "Requested device index: %d, name: \"%s\", path \"%s\"",
      object->device_index, GST_STR_NULL (object->device_name),
      GST_STR_NULL (object->device_path));

  for (iter = activate_list; iter; iter = g_list_next (iter)) {
    GstMFDeviceActivate *activate = (GstMFDeviceActivate *) iter->data;
    gboolean match;

    if (object->device_path) {
      match = g_ascii_strcasecmp (activate->path, object->device_path) == 0;
    } else if (object->device_name) {
      match = g_ascii_strcasecmp (activate->name, object->device_name) == 0;
    } else if (object->device_index >= 0) {
      match = activate->index == object->device_index;
    } else {
      /* pick the first entry */
      match = TRUE;
    }

    if (match) {
      target = activate;
      break;
    }
  }

  if (target) {
    object->opened = gst_mf_source_reader_open (self, target->handle);

    g_free (object->device_path);
    object->device_path = g_strdup (target->path);

    g_free (object->device_name);
    object->device_name = g_strdup (target->name);

    object->device_index = target->index;
  }

  if (activate_list)
    g_list_free_full (activate_list,
        (GDestroyNotify) gst_mf_device_activate_free);

run_loop:
  GST_DEBUG_OBJECT (self, "Starting main loop");
  g_main_loop_run (self->loop);
  GST_DEBUG_OBJECT (self, "Stopped main loop");

  gst_mf_source_reader_stop (object);
  gst_mf_source_reader_close (self);

  g_main_context_pop_thread_default (self->context);

  CoUninitialize ();

  return NULL;
}

static gboolean
gst_mf_source_enum_device_activate (GstMFSourceReader * self,
    GstMFSourceType source_type, GList ** device_sources)
{
  HRESULT hr;
  GList *ret = NULL;
  ComPtr<IMFAttributes> attr;
  IMFActivate **devices = NULL;
  UINT32 i, count = 0;

  hr = MFCreateAttributes (&attr, 1);
  if (!gst_mf_result (hr)) {
    return FALSE;
  }

  switch (source_type) {
    case GST_MF_SOURCE_TYPE_VIDEO:
      hr = attr->SetGUID (MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unknown source type %d", source_type);
      return FALSE;
  }

  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFEnumDeviceSources (attr.Get (), &devices, &count);
  if (!gst_mf_result (hr))
    return FALSE;

  for (i = 0; i < count; i++) {
    GstMFDeviceActivate *entry;
    LPWSTR name;
    UINT32 name_len;
    IMFActivate *activate = devices[i];

    switch (source_type) {
      case GST_MF_SOURCE_TYPE_VIDEO:
        hr = activate->GetAllocatedString (
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &name, &name_len);
        break;
      default:
        g_assert_not_reached ();
        goto done;
    }

    entry = g_new0 (GstMFDeviceActivate, 1);
    entry->index = i;
    entry->handle = activate;

    if (gst_mf_result (hr)) {
      entry->path = g_utf16_to_utf8 ((const gunichar2 *) name,
          -1, NULL, NULL, NULL);
      CoTaskMemFree (name);
    }

    hr = activate->GetAllocatedString (MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
        &name, &name_len);
    if (gst_mf_result (hr)) {
      entry->name = g_utf16_to_utf8 ((const gunichar2 *) name,
          -1, NULL, NULL, NULL);
      CoTaskMemFree (name);
    }

    ret = g_list_prepend (ret, entry);
  }

done:
  ret = g_list_reverse (ret);
  CoTaskMemFree (devices);

  *device_sources = ret;

  return ! !ret;
}

static void
gst_mf_device_activate_free (GstMFDeviceActivate * activate)
{
  g_return_if_fail (activate != NULL);

  if (activate->handle)
    activate->handle->Release ();

  g_free (activate->name);
  g_free (activate->path);
  g_free (activate);
}

GstMFSourceObject *
gst_mf_source_reader_new (GstMFSourceType type, gint device_index,
    const gchar * device_name, const gchar * device_path)
{
  GstMFSourceObject *self;

  /* TODO: add more type */
  g_return_val_if_fail (type == GST_MF_SOURCE_TYPE_VIDEO, NULL);

  self = (GstMFSourceObject *) g_object_new (GST_TYPE_MF_SOURCE_READER,
      "source-type", type, "device-index", device_index, "device-name",
      device_name, "device-path", device_path, NULL);

  gst_object_ref_sink (self);

  if (!self->opened) {
    GST_WARNING_OBJECT (self, "Couldn't open device");
    gst_object_unref (self);
    return NULL;
  }

  return self;
}