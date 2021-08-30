#ifndef __GST_NDI_DEVICE_H__
#define __GST_NDI_DEVICE_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>

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

GList* gst_decklink_get_devices(void);

typedef struct _GstNdiInput GstNdiInput;
struct _GstNdiInput {
    /* Everything below protected by mutex */
    GMutex lock;
    GThread* read_thread;
    gboolean is_read_terminated;

    NDIlib_recv_instance_t pNDI_recv;
    /* Set by the video source */
    void (*got_video_frame) (GstElement* ndi_device, gint8* buffer, guint size);

    /* Set by the audio source */
    void (*got_audio_frame) (GstElement* ndi_device, gint8* buffer, guint size, guint stride);

    gboolean is_started;

    GstElement* audiosrc;
    gboolean audio_enabled;
    GstElement* videosrc;
    gboolean video_enabled;

    int xres, yres;
    int frame_rate_N, frame_rate_D;
    NDIlib_frame_format_type_e frame_format_type;
    NDIlib_FourCC_video_type_e FourCC;

    guint channels;
    guint sample_rate;
};

typedef struct _GstNdiOutput GstNdiOutput;
struct _GstNdiOutput {
    /* Everything below protected by mutex */
    GMutex lock;
};

GstNdiInput * gst_ndi_acquire_input(const char* id, GstElement * src, gboolean is_audio);
void         gst_ndi_release_input(const char* id, GstElement * src, gboolean is_audio);

GType gst_ndi_device_get_type(void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstNdiDevice, gst_object_unref)

G_END_DECLS

#endif /* __GST_MF_DEVICE_H__ */
