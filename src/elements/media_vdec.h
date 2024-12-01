#ifndef MY_VDEC_H
#define MY_VDEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define TYPE_MEDIA_VDEC media_vdec_get_type()
G_DECLARE_FINAL_TYPE(MediaVdec, media_vdec, , MEDIA_VDEC, GstElement)
gboolean media_vdec_plugin_init(GstPlugin *plugin);
G_END_DECLS

#endif // MY_VDEC_H
