#define PACKAGE "my_plugin"
#include "my_parser.h"
#include <gst/gst.h>

typedef struct _MyParserPrivate {
    GstElement *parse;
    GstPad *sinkpad;
    GstPad *srcpad;
} MyParserPrivate;

struct _MyParser {
    GstBin parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE(MyParser, my_parser, GST_TYPE_BIN)

// 定义 sink pad 的 Pad Template
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264; video/x-h265")
);

// 定义 src pad 的 Pad Template
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS("video/x-h264; video/x-h265")
);

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    MyParser *self = MY_PARSER(data);

    // 检查是否已存在 src pad
    if (gst_element_get_static_pad(GST_ELEMENT(self), "src") != NULL) {
        g_print("src Pad already exists, skipping\n");
        return;
    }

    g_print("Dynamic pad '%s' created, wrapping it as a ghost pad\n", GST_PAD_NAME(pad));

    // 创建 Ghost Pad
    GstPad *ghost_pad = gst_ghost_pad_new("src", pad);
    gst_pad_set_active(ghost_pad, TRUE);

    // 将 Ghost Pad 添加到 Bin
    gst_element_add_pad(GST_ELEMENT(self), ghost_pad);
}

static gboolean my_parser_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret;
    MyParser *self = MY_PARSER(parent);
    MyParserPrivate *priv = my_parser_get_instance_private(self);
    g_print("my_parser_sink_event: function is called for event: %s\n", GST_EVENT_TYPE_NAME(event));
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            g_print("Caps event received with caps: %s\n", gst_caps_to_string(caps));

            // 创建解析器
            if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-h264"))) {
                priv->parse = gst_element_factory_make("h264parse", NULL);
                g_print("Creating h264parse element\n");
            } else if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-h265"))) {
                priv->parse = gst_element_factory_make("h265parse", NULL);
                g_print("Creating h265parse element\n");
            } else {
                g_print("Unsupported caps format\n");
                return FALSE;
            }

            if (!priv->parse) {
                g_print("Failed to create parser element\n");
                return FALSE;
            }

            gst_bin_add(GST_BIN(self), priv->parse);
            gst_element_sync_state_with_parent(priv->parse);

            // 获取解析器的 sink Pad
            GstPad *parser_sink_pad = gst_element_get_static_pad(priv->parse, "sink");
            GstGhostPad *bin_sink_pad = GST_GHOST_PAD(priv->sinkpad);

            // 将 Bin 的 sink Ghost Pad 的目标设置为解析器的 sink Pad
            if (!gst_ghost_pad_set_target(bin_sink_pad, parser_sink_pad)) {
                g_warning("Failed to set ghost pad target");
                return FALSE;
            }

            g_object_unref(parser_sink_pad);

            // 获取解析器的 src Pad
            GstPad *parser_src_pad = gst_element_get_static_pad(priv->parse, "src");

            // 创建并添加 Bin 的 src Ghost Pad
            if (gst_element_get_static_pad(GST_ELEMENT(self), "src") == NULL) {
                GstPad *ghost_src_pad = gst_ghost_pad_new("src", parser_src_pad);
                gst_pad_set_active(ghost_src_pad, TRUE);
                gst_element_add_pad(GST_ELEMENT(self), ghost_src_pad);
                g_print("Created and added src ghost pad\n");
            }

            g_object_unref(parser_src_pad);

            break;
        }
        default:
            break;
    }

    // 将事件传递给下一个元素
    ret = gst_pad_event_default(pad, parent, event);
    return ret;
}

static GstStateChangeReturn my_parser_change_state(GstElement *element, GstStateChange transition) {
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    MyParser *self = MY_PARSER(element);
    MyParserPrivate *priv = my_parser_get_instance_private(self);

    g_print("my_parser_change_state: transition: %d\n", transition);
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            // Initialize or configure elements if needed
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            // Handle transition from READY to PAUSED if needed
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            // Handle transition from PAUSED to PLAYING if needed
            break;
        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(my_parser_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_READY_TO_NULL:
            // Clean up resources if needed
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            // Handle transition from PAUSED to READY if needed
            break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            // Handle transition from PLAYING to PAUSED if needed
            break;
        default:
            break;
    }

    return ret;
}

static void my_parser_init(MyParser *self) {
    MyParserPrivate *priv = my_parser_get_instance_private(self);

    // 创建一个没有目标的 Ghost Pad
    GstPad *sink_pad = gst_ghost_pad_new_no_target("sink", GST_PAD_SINK);
    gst_pad_set_event_function(sink_pad, my_parser_sink_event);
    gst_element_add_pad(GST_ELEMENT(self), sink_pad);

    priv->sinkpad = sink_pad; // 保存 sink pad 的引用

    priv->parse = NULL;
    priv->srcpad = NULL;
}

static void my_parser_class_init(MyParserClass *klass) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    element_class->change_state = my_parser_change_state;

    // 注册 sink pad 和 src pad 的 Pad Template
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
}

gboolean my_parser_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "myparser", GST_RANK_NONE, MY_TYPE_PARSER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    myparser,
    "My custom parser plugin",
    my_parser_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
