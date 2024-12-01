#include <gst/gst.h>
#include "my_demux.h"
#include "video_player.h"
#include "media_parser.h"
#include "media_vdec.h"
// Plugin initialization function declaration (must be consistent with the definition in my_demux.c)
extern gboolean plugin_init(GstPlugin *plugin);

static void on_demuxer_pad_added(GstElement *element, GstPad *pad, gpointer data);
static void on_parser_pad_added(GstElement *element, GstPad *pad, gpointer data);
static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data);

// Define your own main function
int video_player(const char *file_path) {
    GstElement *pipeline, *demuxer, *queue, *parser, *media_vdec, *video_sink;
    GstBus *bus;

    gst_init(NULL, NULL);
    
    if (!plugin_init(NULL)) {
        g_printerr("video_player: Failed to initialize demux plugin.\n");
        return -1;
    }

    if (!media_parser_plugin_init(NULL)) {
        g_printerr("video_player: Failed to initialize parser plugin.\n");
        return -1;
    }

    if (!media_vdec_plugin_init(NULL)) {
        g_printerr("video_player: Failed to initialize media_vdec plugin.\n");
        return -1;
    }

    g_print("video_player: Entering my_main function.\n");

    // Create GStreamer elements
    pipeline = gst_pipeline_new("mp4-player");
    demuxer = gst_element_factory_make("mydemux", "demuxer");
    queue = gst_element_factory_make("queue", "queue");
    parser = gst_element_factory_make("media_parser", "parser");
    media_vdec = gst_element_factory_make("media_vdec", "decoder");
    video_sink = gst_element_factory_make("glimagesink", "video_sink");

    if (!pipeline || !demuxer || !queue || !parser || !media_vdec || !video_sink) {
        g_printerr("video_player: Failed to create one or more GStreamer elements.\n");
        return -1;
    }

    // Set the size of the queue to 20 frames
    g_object_set(G_OBJECT(queue), "max-size-buffers", 20, NULL);

    // Set input file path for the demuxer
    g_object_set(G_OBJECT(demuxer), "location", file_path, NULL);

    // Add elements to the pipeline
    gst_bin_add_many(GST_BIN(pipeline), demuxer, queue, parser, media_vdec, video_sink, NULL);

    // Handle demuxer's dynamic pad
    g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_demuxer_pad_added), queue);

    // Handle parser's dynamic pad
    g_signal_connect(parser, "pad-added", G_CALLBACK(on_parser_pad_added), media_vdec);

    // Link queue to parser
    if (!gst_element_link(queue, parser)) {
        g_printerr("video_player: Failed to link queue to parser.\n");
        return -1;
    }

    // Link media_vdec to video_sink
    if (!gst_element_link(media_vdec, video_sink)) {
        g_printerr("video_player: Failed to link media_vdec to video_sink.\n");
        return -1;
    }

    // Get the message bus
    bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_callback, NULL);

    // Start playback
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_print("video_player: start playing.\n");

    // Main loop
    GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);

    // Clean up
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}

static void on_demuxer_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *queue = GST_ELEMENT(data);
    GstPad *queue_sink_pad = gst_element_get_static_pad(queue, "sink");
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (caps) {
        gchar *caps_str = gst_caps_to_string(caps);
        g_print("on_demuxer_pad_added Pad caps: %s\n", caps_str);
        g_free(caps_str);
        gst_caps_unref(caps);
    } else {
        g_print("on_demuxer_pad_added Pad has no caps\n");
    }

    if (gst_pad_link(pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        g_warning("Failed to link demuxer pad to queue sink pad");
    } else {
        g_print("Successfully linked demuxer pad to queue sink pad\n");
    }

    g_object_unref(queue_sink_pad);
}

static void on_parser_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *media_vdec = GST_ELEMENT(data);
    GstPad *vdec_sink_pad = gst_element_get_static_pad(media_vdec, "sink");
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (caps) {
        gchar *caps_str = gst_caps_to_string(caps);
        g_print("on_parser_pad_added Pad caps: %s\n", caps_str);
        g_free(caps_str);
        gst_caps_unref(caps);
    } else {
        g_print("on_parser_pad_added Pad has no caps\n");
    }

    if (gst_pad_link(pad, vdec_sink_pad) != GST_PAD_LINK_OK) {
        g_warning("Failed to link parser pad to media_vdec sink pad");
    } else {
        g_print("Successfully linked parser pad to media_vdec sink pad\n");
    }

    g_object_unref(vdec_sink_pad);
}

static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data) {
    GstElement *pipeline = GST_ELEMENT(data);
    g_print("bus_callback called with message type: %s\n", GST_MESSAGE_TYPE_NAME(msg));
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_error_free(err);
            g_free(debug_info);
            g_main_loop_quit((GMainLoop *)data);
            break;
        }
        case GST_MESSAGE_EOS:
            g_print("End-Of-Stream reached.\n");
            g_main_loop_quit((GMainLoop *)data);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            g_print("Element %s state changed from %s to %s:\n",
                    GST_OBJECT_NAME(msg->src),
                    gst_element_state_get_name(old_state),
                    gst_element_state_get_name(new_state));
            break;
        }
        case GST_MESSAGE_QOS: {
            g_print("QoS message received from element %s (%s)\n", GST_OBJECT_NAME(msg->src), GST_ELEMENT_NAME(msg->src));
            break;
        }
        case GST_MESSAGE_TAG: {
            GstTagList *tags = NULL;
            gst_message_parse_tag(msg, &tags);
            g_print("Tag message received from element %s (%s)\n", GST_OBJECT_NAME(msg->src), GST_ELEMENT_NAME(msg->src));
            gchar *tag_str = gst_tag_list_to_string(tags);
            g_print("Tags: %s\n", tag_str);
            g_free(tag_str);
            gst_tag_list_unref(tags);
            break;
        }
        default:
            break;
    }
    return TRUE;
}
