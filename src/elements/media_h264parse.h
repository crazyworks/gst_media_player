#ifndef MEDIA_H264PARSE_H
#define MEDIA_H264PARSE_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define TYPE_MEDIA_H264PARSE media_h264parse_get_type()
G_DECLARE_FINAL_TYPE(MediaH264Parse, media_h264parse, , H264PARSE, GstElement)

G_END_DECLS

#endif // MEDIA_H264PARSE_H
