#ifndef __GST_NDI_DEVICE_H__
#define __GST_NDI_DEVICE_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>
#include <gst/base/base.h>
G_BEGIN_DECLS

#define GST_TYPE_NDI_DEVICE          (gst_ndi_device_get_type())
#define GST_NDI_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NDI_DEVICE,GstNdiDevice))

typedef struct _GstNdiDevice GstNdiDevice;
typedef struct _GstNdiDeviceClass GstNdiDeviceClass;

struct _GstNdiDeviceClass
{
    GstDeviceClass parent_class;
};

struct _GstNdiDevice
{
    GstDevice parent;
    gchar* device_path;
    gboolean isVideo;
};

GList* gst_ndi_device_get_devices(void);

typedef struct _GstNdiInput GstNdiInput;
struct _GstNdiInput {
    GMutex lock;
    GThread* capture_thread;
    gboolean is_capture_terminated;

    gboolean is_started;
    NDIlib_recv_instance_t pNDI_recv;
    NDIlib_framesync_instance_t pNDI_recv_sync;
    
    /* Set by the video source */
    void (*got_video_frame) (GstElement* ndi_device, gint8* buffer, guint size, bool is_caps_changed);
    GstElement* videosrc;
    gboolean is_video_enabled;
    int xres;
    int yres;
    int frame_rate_N;
    int frame_rate_D;
    NDIlib_frame_format_type_e frame_format_type;
    NDIlib_FourCC_video_type_e FourCC;
    int stride;

    /* Set by the audio source */
    void (*got_audio_frame) (GstElement* ndi_device, gint8* buffer, guint size, guint stride);
    GstElement* audiosrc;
    gboolean is_audio_enabled;
    guint channels;
    guint sample_rate;
    guint audio_buffer_size;
};

typedef struct _GstNdiOutput GstNdiOutput;
struct _GstNdiOutput {
    GMutex lock;
};

GstNdiInput * gst_ndi_device_acquire_input(const char* id, GstElement * src, gboolean is_audio);
void          gst_ndi_device_release_input(const char* id, GstElement * src, gboolean is_audio);
void          gst_ndi_device_ref(void);
void          gst_ndi_device_unref(void);
void          gst_ndi_device_src_send_caps_event(GstBaseSrc* element, GstCaps* caps);

GType gst_ndi_device_get_type(void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstNdiDevice, gst_object_unref)

G_END_DECLS

#endif /* __GST_NDI_DEVICE_H__ */
