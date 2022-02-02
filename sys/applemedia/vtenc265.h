/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifndef __GST_VTENC_265_H__
#define __GST_VTENC_265_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <VideoToolbox/VideoToolbox.h>

G_BEGIN_DECLS

#define GST_VTENC_265_CAST(obj) ((GstVTEnc265 *) (obj))
#define GST_VTENC_265_CLASS_GET_CODEC_DETAILS(klass) \
  ((const GstVTEncoder265Details *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), \
      GST_VTENC_CODEC_DETAILS_QDATA))

typedef struct _GstVTEncoder265Details GstVTEncoder265Details;
typedef struct _GstVTEnc265ClassParams GstVTEnc265ClassParams;
typedef struct _GstVTEnc265Class GstVTEnc265Class;
typedef struct _GstVTEnc265 GstVTEnc265;

struct _GstVTEncoder265Details
{
  const gchar * name;
  const gchar * element_name;
  const gchar * mimetype;
  CMVideoCodecType format_id;
  gboolean require_hardware;
};

struct _GstVTEnc265Class
{
  GstVideoEncoderClass parent_class;
};

struct _GstVTEnc265
{
  GstVideoEncoder parent;

  const GstVTEncoder265Details * details;

  CFStringRef profile_level;
  guint bitrate;
  gboolean allow_frame_reordering;
  gboolean realtime;
  gdouble quality;
  gint max_keyframe_interval;
  GstClockTime max_keyframe_interval_duration;
  gint latency_frames;

  gboolean dump_properties;
  gboolean dump_attributes;

  gint negotiated_width, negotiated_height;
  gint negotiated_fps_n, negotiated_fps_d;
  gint caps_width, caps_height;
  gint caps_fps_n, caps_fps_d;
  GstVideoCodecState *input_state;
  GstVideoInfo video_info;
  VTCompressionSessionRef session;
  CFDictionaryRef keyframe_props;

  GAsyncQueue * cur_outframes;
};

void gst_vtenc_265_register_elements (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_VTENC_265_H__ */
