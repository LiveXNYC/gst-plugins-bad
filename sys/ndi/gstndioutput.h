#ifndef __GST_NDI_OUTPUT_H__
#define __GST_NDI_OUTPUT_H__

#include <gst/gst.h>
#include <ndi/Processing.NDI.Lib.h>

typedef struct _GstNdiOutput GstNdiOutput;
struct _GstNdiOutput {
    GMutex lock;
};

#endif /* __GST_NDI_OUTPUT_H__ */