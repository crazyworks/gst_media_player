#ifndef MEDIA_AUDIORENDER_H
#define MEDIA_AUDIORENDER_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define TYPE_MEDIA_AUDIORENDER media_audiorender_get_type()
G_DECLARE_FINAL_TYPE(MediaAudioRender, media_audiorender, MEDIA, AUDIORENDER, GstElement)

gboolean media_audiorender_plugin_init(GstPlugin *plugin);

G_END_DECLS

#endif // MEDIA_AUDIORENDER_H
