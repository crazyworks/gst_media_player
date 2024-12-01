#define PACKAGE "my_plugin"
#include "media_parser.h"
#include <gst/gst.h>

typedef struct _MediaParserPrivate {
    GstElement *parse;
    GstPad *sinkpad;
    GstPad *srcpad;
} MediaParserPrivate;

struct _MediaParser {
    GstBin parent;
};


G_DEFINE_TYPE_WITH_PRIVATE(MediaParser, media_parser, GST_TYPE_BIN)

// 定义 sink pad 的 Pad Template
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264; video/x-h265; video/x-vp9")
);

// 定义 src pad 的 Pad Template
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-h264, parsed=(boolean)true, "
        "stream-format=(string){ avc, avc3, byte-stream }, "
        "alignment=(string){ au, nal }; "
        "video/x-h265, parsed=(boolean)true, "
        "stream-format=(string){ hvc1, hev1, byte-stream }, "
        "alignment=(string){ au, nal }"
    )
);

static GstPadProbeReturn pad_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    MediaParser *self = MEDIA_PARSER(user_data);
    MediaParserPrivate *priv = media_parser_get_instance_private(self);
    GstCaps *caps = gst_pad_get_current_caps(pad);

    if (caps) {
        gchar *caps_str = gst_caps_to_string(caps);
        g_print("Caps detected by probe: %s\n", caps_str);
        g_free(caps_str);

        // 创建解析器
        if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-h264"))) {
            priv->parse = gst_element_factory_make("h264parse", NULL);
            g_print("Creating h264parse element\n");
        } else if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-h265"))) {
            priv->parse = gst_element_factory_make("h265parse", NULL);
            g_print("Creating h265parse element\n");
        } else if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-vp9"))) {
            priv->parse = gst_element_factory_make("vp9parse", NULL);
            g_print("Creating vp9parse element\n");
        } else {
            g_print("Unsupported caps format, bypassing parser\n");
            priv->parse = NULL;
        }

        if (priv->parse) {
            gst_bin_add(GST_BIN(self), priv->parse);
            gst_element_sync_state_with_parent(priv->parse);

            // 获取解析器的 sink Pad
            GstPad *parser_sink_pad = gst_element_get_static_pad(priv->parse, "sink");
            GstGhostPad *bin_sink_pad = GST_GHOST_PAD(priv->sinkpad);

            // 将 Bin 的 sink Ghost Pad 的目标设置为解析器的 sink Pad
            if (!gst_ghost_pad_set_target(bin_sink_pad, parser_sink_pad)) {
                g_warning("Failed to set ghost pad target");
                return GST_PAD_PROBE_REMOVE;
            }

            g_object_unref(parser_sink_pad);

            // 获取解析器的 src Pad
            GstPad *parser_src_pad = gst_element_get_static_pad(priv->parse, "src");

            // 检查是否已经存在名为 "src" 的 Pad
            if (gst_element_get_static_pad(GST_ELEMENT(self), "src")) {
                g_warning("Padname src is not unique in element parser, not adding");
                g_object_unref(parser_src_pad);
                return GST_PAD_PROBE_REMOVE;
            }

            // 创建并添加 Bin 的 src Ghost Pad，不设置caps
            GstPad *ghost_src_pad = gst_ghost_pad_new("src", parser_src_pad);
            gst_pad_set_active(ghost_src_pad, TRUE);
            gst_element_add_pad(GST_ELEMENT(self), ghost_src_pad);
            g_print("Created and added src ghost pad without setting caps\n");

            g_object_unref(parser_src_pad);
        } else {
            // 直接将 sink pad 连接到 src pad
            GstPad *sink_pad = priv->sinkpad;
            GstPad *src_pad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
            if (!src_pad) {
                src_pad = gst_ghost_pad_new_no_target("src", GST_PAD_SRC);
                gst_pad_set_active(src_pad, TRUE);
                gst_element_add_pad(GST_ELEMENT(self), src_pad);
            }
            gst_ghost_pad_set_target(GST_GHOST_PAD(src_pad), sink_pad);
            g_print("Bypassing parser, directly linking sink pad to src pad\n");
        }

        gst_caps_unref(caps);
        return GST_PAD_PROBE_REMOVE;
    }
    return GST_PAD_PROBE_OK;
}

static gboolean media_parser_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret;
    MediaParser *self = MEDIA_PARSER(parent);
    g_print("media_parser_sink_event: function is called for event: %s\n", GST_EVENT_TYPE_NAME(event));
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            gchar *caps_str = gst_caps_to_string(caps);
            g_print("media_parser_sink_event: Received CAPS event with caps: %s\n", caps_str);
            g_free(caps_str);
            // gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, pad_probe_callback, self, NULL);
            break;
        }
        default:
            break;
    }

    // 将事件传递给下一个元素
    ret = gst_pad_event_default(pad, parent, event);
    return ret;
}

static gboolean media_parser_src_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret;
    MediaParser *self = MEDIA_PARSER(parent);
    g_print("media_parser_src_event: function is called for event: %s\n", GST_EVENT_TYPE_NAME(event));
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            gchar *caps_str = gst_caps_to_string(caps);
            g_print("media_parser_src_event: Received CAPS event with caps: %s\n", caps_str);
            g_free(caps_str);
            break;
        }
        default:
            break;
    }

    // 将事件传递给下一个元素
    ret = gst_pad_event_default(pad, parent, event);
    return ret;
}

static GstStateChangeReturn media_parser_change_state(GstElement *element, GstStateChange transition) {
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    MediaParser *self = MEDIA_PARSER(element);
    MediaParserPrivate *priv = media_parser_get_instance_private(self);

    g_print("media_parser_change_state: transition: %d\n", transition);
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            g_print("media_parser_change_state: NULL_TO_READY\n");
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            g_print("media_parser_change_state: READY_TO_PAUSED\n");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            g_print("media_parser_change_state: PAUSED_TO_PLAYING\n");
            break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            g_print("media_parser_change_state: PLAYING_TO_PAUSED\n");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            g_print("media_parser_change_state: PAUSED_TO_READY\n");
            break;
        case GST_STATE_CHANGE_READY_TO_NULL:
            g_print("media_parser_change_state: READY_TO_NULL\n");
            break;
        default:
            g_print("media_parser_change_state: Unhandled transition\n");
            break;
    }

    ret = GST_ELEMENT_CLASS(media_parser_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_READY_TO_NULL:
            g_print("media_parser_change_state: READY_TO_NULL\n");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            g_print("media_parser_change_state: PAUSED_TO_READY\n");
            break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            g_print("media_parser_change_state: PLAYING_TO_PAUSED\n");
            break;
        case GST_STATE_CHANGE_NULL_TO_READY:
            g_print("media_parser_change_state: NULL_TO_READY\n");
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            g_print("media_parser_change_state: READY_TO_PAUSED\n");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            g_print("media_parser_change_state: PAUSED_TO_PLAYING\n");
            break;
        default:
            g_print("media_parser_change_state: Unhandled transition\n");
            break;
    }

    return ret;
}

static void media_parser_init(MediaParser *self) {
    MediaParserPrivate *priv = media_parser_get_instance_private(self);

    // 创建一个没有目标的 Ghost Pad
    GstPad *sink_pad = gst_ghost_pad_new_no_target("sink", GST_PAD_SINK);
    gst_pad_set_event_function(sink_pad, media_parser_sink_event);
    gst_element_add_pad(GST_ELEMENT(self), sink_pad);

    priv->sinkpad = sink_pad; // 保存 sink pad 的引用

    priv->parse = NULL;
    priv->srcpad = NULL;

    // 在初始化时绑定 pad probe
    gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, pad_probe_callback, self, NULL);

}

static void media_parser_class_init(MediaParserClass *klass) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    element_class->change_state = media_parser_change_state;

    // 注册 sink pad 和 src pad 的 Pad Template
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
}

gboolean media_parser_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "media_parser", GST_RANK_NONE, MEDIA_TYPE_PARSER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    media_parser,
    "My custom media parser plugin",
    media_parser_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
