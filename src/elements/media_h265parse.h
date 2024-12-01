#ifndef MEDIA_H265PARSE_H
#define MEDIA_H265PARSE_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define TYPE_MEDIA_H265PARSE media_h265parse_get_type()
G_DECLARE_FINAL_TYPE(MediaH265Parse, media_h265parse, , H265PARSE, GstElement)

G_END_DECLS

#endif // MEDIA_H265PARSE_H
