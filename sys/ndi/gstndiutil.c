#include "gstndiutil.h"

const gchar*
gst_ndi_util_get_format(NDIlib_FourCC_video_type_e fourCC) {
    const gchar* res = NULL;
    switch (fourCC) {
    case NDIlib_FourCC_type_UYVY:
        res = "UYVY";
        break;
    case NDIlib_FourCC_type_UYVA:
        res = "UYVA";
        break;
    case NDIlib_FourCC_type_P216:
        res = "P216";
        break;
    case NDIlib_FourCC_type_PA16:
        res = "PA16";
        break;
    case NDIlib_FourCC_type_YV12:
        res = "YV12";
        break;
    case NDIlib_FourCC_type_I420:
        res = "I420";
        break;
    case NDIlib_FourCC_type_NV12:
        res = "NV12";
        break;
    case NDIlib_FourCC_type_BGRA:
        res = "BGRA";
        break;
    case NDIlib_FourCC_type_BGRX:
        res = "BGRX";
        break;
    case NDIlib_FourCC_type_RGBA:
        res = "RGBA";
        break;
    case NDIlib_FourCC_type_RGBX:
        res = "RGBX";
        break;
    default:
        break;
    }
    return res;
}

const gchar*
gst_ndi_util_get_frame_format(NDIlib_frame_format_type_e frameFormat) {
    const gchar* res = NULL;
    switch (frameFormat) {
    case NDIlib_frame_format_type_progressive:
        res = "progressive";
        break;
    case NDIlib_frame_format_type_interleaved:
        res = "interleaved";
        break;
    case NDIlib_frame_format_type_field_0:
    case NDIlib_frame_format_type_field_1:
        res = "alternate";
        break;
    default:
        break;
    }
    return res;
}

GstCaps* gst_util_create_default_video_caps(void) {
    return gst_caps_new_any();
}

GstCaps* gst_util_create_default_audio_caps(void) {
    return gst_caps_from_string("audio/x-raw, format=F32LE, channels=[1, 16], rate={44100, 48000, 96000}, layout=interleaved");
}
