/* GStreamer H.264 Parser
 * Copyright (C) <2010> Collabora ltd
 * Copyright (C) <2010> Nokia Corporation
 * Copyright (C) <2011> Intel Corporation
 *
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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

#ifndef __GST_H264_PARSE_H__
#define __GST_H264_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/video/video.h>
#include "gstvideoparseutils.h"

G_BEGIN_DECLS

typedef struct _H264Params H264Params;

#define GST_TYPE_H264_PARSE \
  (gst_h264_parse_get_type())
#define GST_H264_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_PARSE,GstH264Parse))
#define GST_H264_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_PARSE,GstH264ParseClass))
#define GST_IS_H264_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_PARSE))
#define GST_IS_H264_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_PARSE))

GType gst_h264_parse_get_type (void);

typedef struct _GstH264Parse GstH264Parse;
typedef struct _GstH264ParseClass GstH264ParseClass;

struct _GstH264Parse
{
  GstBaseParse baseparse;

  /* stream */
  gint width, height;
  gint fps_num, fps_den;
  gint upstream_par_n, upstream_par_d;
  gint parsed_par_n, parsed_par_d;
  gint parsed_fps_n, parsed_fps_d;
  GstVideoColorimetry parsed_colorimetry;
  /* current codec_data in output caps, if any */
  GstBuffer *codec_data;
  /* input codec_data, if any */
  GstBuffer *codec_data_in;
  guint nal_length_size;
  gboolean packetized;
  gboolean split_packetized;
  gboolean transform;

  /* state */
  GstH264NalParser *nalparser;
  guint state;
  guint in_align;
  guint align;
  guint format;
  gint current_off;
  /* True if input format and alignment match negotiated output */
  gboolean can_passthrough;

  GstClockTime last_report;
  gboolean push_codec;
  /* The following variables have a meaning in context of "have
   * SPS/PPS to push downstream", e.g. to update caps */
  gboolean have_sps;
  gboolean have_pps;

  /* per frame sps/pps check for periodic push codec decision */
  gboolean have_sps_in_frame;
  gboolean have_pps_in_frame;

  gboolean first_frame;

  /* collected SPS and PPS NALUs */
  GstBuffer *sps_nals[GST_H264_MAX_SPS_COUNT];
  GstBuffer *pps_nals[GST_H264_MAX_PPS_COUNT];

  /* collected SEI timestamps */
  guint num_clock_timestamp;
  GstH264PicTiming pic_timing_sei;

  /* Infos we need to keep track of */
  guint32 sei_cpb_removal_delay;
  guint8 sei_pic_struct;
  guint8 sei_pic_struct_pres_flag;
  guint field_pic_flag;

  /* cached timestamps */
  /* (trying to) track upstream dts and interpolate */
  GstClockTime dts;
  /* dts at start of last buffering period */
  GstClockTime ts_trn_nb;
  gboolean do_ts;

  gboolean discont;
  gboolean marker;

  /* frame parsing */
  /*guint last_nal_pos;*/
  /*guint next_sc_pos;*/
  gint idr_pos, sei_pos;
  gint pic_timing_sei_pos;
  gint pic_timing_sei_size;
  gint force_pic_timing_sei_pos;
  guint pic_struct_present_flag_position;
  guint sps_nals_pic_struct_present_flag_position;
  guint sps_nals_timing_info_present_flag;
  guint vui_parameters_present_flag_position;
  gboolean update_caps;
  GstAdapter *frame_out;
  gboolean keyframe;
  gboolean predicted;
  gboolean bidirectional;
  gboolean header;
  gboolean frame_start;
  guint8 cpb_removal_delay_length_minus1;
  guint8 dpb_output_delay_length_minus1;
  guint8 time_offset_length;
  guint64 total_key_frames;
  /* AU state */
  gboolean picture_start;

  /* props */
  gint interval;
  gboolean update_timecode;

  GstClockTime pending_key_unit_ts;
  GstEvent *force_key_unit_event;

  /* Stereo / multiview info */
  GstVideoMultiviewMode multiview_mode;
  GstVideoMultiviewFlags multiview_flags;
  gboolean first_in_bundle;

  /* For insertion of AU Delimiter */
  gboolean aud_needed;
  gboolean aud_insert;

  GstVideoParseUserData user_data;

  GstVideoMasteringDisplayInfo mastering_display_info;
  guint mastering_display_info_state;

  GstVideoContentLightLevel content_light_level;
  guint content_light_level_state;

  /* For forward predicted trickmode */
  gboolean discard_bidirectional;
};

struct _GstH264ParseClass
{
  GstBaseParseClass parent_class;
};

G_END_DECLS
#endif
