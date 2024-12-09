#ifndef MEDIA_VIDEORENDER_H
#define MEDIA_VIDEORENDER_H

#include <gst/video/video.h>

G_BEGIN_DECLS

#define TYPE_MEDIA_VIDEORENDER media_videorender_get_type()
G_DECLARE_FINAL_TYPE(MediaVideoRender, media_videorender, MEDIA, VIDEORENDER, GstVideoSink)

G_END_DECLS

#endif // MEDIA_VIDEORENDER_H
