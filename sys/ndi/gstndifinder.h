#ifndef __GST_NDI_FINDER_H__
#define __GST_NDI_FINDER_H__

#include <ndi/Processing.NDI.Lib.h>

void gst_ndi_finder_create(void);
void gst_ndi_finder_release(void);
const NDIlib_source_t* gst_ndi_finder_get_sources(uint32_t* no_sources);

#endif /* __GST_NDI_FINDER_H__ */