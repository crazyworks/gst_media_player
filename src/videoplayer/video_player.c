#include <gst/gst.h>
#include "my_demux.h"
#include "video_player.h"
#include "my_parser.h"
// Plugin initialization function declaration (must be consistent with the definition in my_demux.c)
extern gboolean plugin_init(GstPlugin *plugin);

static void on_demuxer_pad_added(GstElement *element, GstPad *pad, gpointer data);
static void on_parser_pad_added(GstElement *element, GstPad *pad, gpointer data);

// Define your own main function
int video_player(const char *file_path) {
    GstElement *pipeline, *demuxer, *queue, *myparser, *decoder, *video_sink;
    GstBus *bus;
    GstMessage *msg;

    gst_init(NULL, NULL);
    
    if (!plugin_init(NULL)) {
        g_printerr("video_player: Failed to initialize demux plugin.\n");
        return -1;
    }

    if (!my_parser_plugin_init(NULL)) {
        g_printerr("video_player: Failed to initialize parser plugin.\n");
        return -1;
    }

    g_print("video_player: Entering my_main function.\n");

    // Create GStreamer elements
    pipeline = gst_pipeline_new("mp4-player");
    demuxer = gst_element_factory_make("mydemux", "demuxer");
    queue = gst_element_factory_make("queue", "queue");
    myparser = gst_element_factory_make("myparser", "myparser");
    decoder = gst_element_factory_make("avdec_h264", "decoder");
    video_sink = gst_element_factory_make("glimagesink", "video_sink");

    if (!pipeline || !demuxer || !queue || !myparser || !decoder || !video_sink) {
        g_printerr("video_player: Failed to create one or more GStreamer elements.\n");
        return -1;
    }

    // Set input file path for the demuxer
    g_object_set(G_OBJECT(demuxer), "location", file_path, NULL);

    // Add elements to the pipeline
    gst_bin_add_many(GST_BIN(pipeline), demuxer, queue, myparser, decoder, video_sink, NULL);

    // Handle demuxer's dynamic pad
    g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_demuxer_pad_added), queue);

    // Link queue to myparser
    if (!gst_element_link(queue, myparser)) {
        g_printerr("video_player: Failed to link queue to myparser.\n");
        return -1;
    }

    // Handle myparser's dynamic pad
    g_signal_connect(myparser, "pad-added", G_CALLBACK(on_parser_pad_added), decoder);

    // Static link decoder to video sink
    if (!gst_element_link(decoder, video_sink)) {
        g_printerr("video_player: Failed to link decoder to video sink.\n");
        return -1;
    }

    // Start playback
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_print("video_player: start playing.\n");

    // Get the message bus
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    // Handle messages
    if (msg != NULL) {
        GError *err;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("video_player: Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug_info);
            break;
        case GST_MESSAGE_EOS:
            g_print("video_player: Playback finished\n");
            break;
        default:
            g_printerr("video_player: Unknown message type\n");
            break;
        }
        gst_message_unref(msg);
    }

    // Clean up
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
static void on_demuxer_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *queue = GST_ELEMENT(data);
    GstPad *queue_sink_pad = gst_element_get_static_pad(queue, "sink");

    if (gst_pad_link(pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        g_warning("Failed to link demuxer pad to queue sink pad");
    } else {
        g_print("Successfully linked demuxer pad to queue sink pad\n");
    }

    g_object_unref(queue_sink_pad);
}

static void on_parser_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *decoder = GST_ELEMENT(data);
    GstPad *decoder_sink_pad = gst_element_get_static_pad(decoder, "sink");

    if (gst_pad_link(pad, decoder_sink_pad) != GST_PAD_LINK_OK) {
        g_warning("Failed to link myparser src pad to decoder sink pad");
    } else {
        g_print("Successfully linked myparser src pad to decoder sink pad\n");
    }

    g_object_unref(decoder_sink_pad);
}
