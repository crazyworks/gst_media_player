#ifndef MY_PARSER_H
#define MY_PARSER_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define MEDIA_TYPE_PARSER (media_parser_get_type())
G_DECLARE_FINAL_TYPE(MediaParser, media_parser, MEDIA,PARSER, GstBin)


GType media_parser_get_type(void);

gboolean media_parser_plugin_init(GstPlugin *plugin);

G_END_DECLS

#endif // MY_PARSER_H

