#ifndef MEDIA_ADEC_H
#define MEDIA_ADEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define MEDIA_TYPE_ADEC media_adec_get_type()
G_DECLARE_FINAL_TYPE(MediaAdec, media_adec, MEDIA, ADEC, GstElement)

gboolean media_adec_plugin_init(GstPlugin *plugin);
struct _MediaAdec{
    GstElement parent;
};

G_END_DECLS

#endif // MEDIA_ADEC_H