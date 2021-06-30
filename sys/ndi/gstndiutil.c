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
    }
    return res;
}

NDIlib_video_frame_v2_t gst_ndi_util_get_video_frame(NDIlib_recv_instance_t instance, gint timeout) {
    NDIlib_video_frame_v2_t video_frame;
    video_frame.xres = 0;
    video_frame.yres = 0;
    NDIlib_frame_type_e res = NDIlib_frame_type_none;
    do {
        res = NDIlib_recv_capture_v2(instance, &video_frame, NULL, NULL, timeout);
    } while (res != NDIlib_frame_type_video && res != NDIlib_frame_type_none);

    return video_frame;
}

GstCaps* gst_util_create_video_caps(const NDIlib_video_frame_v2_t* frame) {
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, gst_ndi_util_get_format(frame->FourCC),
        "width", G_TYPE_INT, (int)frame->xres,
        "height", G_TYPE_INT, (int)frame->yres,
        "framerate", GST_TYPE_FRACTION, frame->frame_rate_N, frame->frame_rate_D,
        "interlace-mode", G_TYPE_STRING, gst_ndi_util_get_frame_format(frame->frame_format_type),
        NULL);

    return caps;
}

GstCaps* gst_util_create_default_videro_caps() {
    return gst_caps_new_any();
    //return gst_caps_from_string("video/x-raw, format=(string) { UYVY, UYVA, BGRA, RGBA }, width=(int) [ 1, 2147483647 ], height=(int) [ 1, 2147483647 ], framerate=(fraction) [ 0/1, 2147483647/1 ]");
    
    /*
    return gst_caps_from_string(
        "video/x-raw, format=(string) UYVY,  width=(int) 1280, height=(int) 720,  framerate=(fraction) 30/1, interlace-mode=(string) progressive;"
        " video/x-raw, format=(string) UYVY, width=(int) 1920, height=(int) 1080, framerate=(fraction) 30/1, interlace-mode=(string) progressive;"
        " video/x-raw, format=(string) UYVY, width=(int) 640, height=(int) 480, framerate=(fraction) 30/1, interlace-mode=(string) progressive");
        */

    /*return gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "UYVY",
        "width", G_TYPE_INT, 1920,
        "height", G_TYPE_INT, 1080,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        "interlace-mode", G_TYPE_STRING, "progressive",
        NULL);*/

    /*
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "UYVY",
        "width", G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        "interlace-mode", G_TYPE_STRING, "progressive",
        NULL);

    GstStructure* s = gst_structure_new("video/x-raw",
        "format", G_TYPE_STRING, "UYVY",
        "width", G_TYPE_INT, 1920,
        "height", G_TYPE_INT, 1080,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        "interlace-mode", G_TYPE_STRING, "progressive",
        NULL);
    gst_caps_merge_structure(caps, s);

    s = gst_structure_new("video/x-raw",
        "format", G_TYPE_STRING, "UYVY",
        "width", G_TYPE_INT, 640,
        "height", G_TYPE_INT, 480,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        "interlace-mode", G_TYPE_STRING, "progressive",
        NULL);
    gst_caps_merge_structure(caps, s);

    return caps;
        */
}
