#ifndef MY_PARSER_H
#define MY_PARSER_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define MY_TYPE_PARSER (my_parser_get_type())
G_DECLARE_FINAL_TYPE(MyParser, my_parser, MY, PARSER, GstBin)


GType my_parser_get_type(void);

gboolean my_parser_plugin_init(GstPlugin *plugin);

G_END_DECLS

#endif // MY_PARSER_H

