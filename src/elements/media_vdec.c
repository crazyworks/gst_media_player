#define PACKAGE "my_plugin"
#include "media_vdec.h"
#include <gst/gst.h>
#include <libavcodec/avcodec.h>
#include <glib.h>
#include <pthread.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h> // Add this line to include the declaration of av_get_pix_fmt_name
#include <stdio.h> // Add this line to include file operation functions

// Define maximum queue size to prevent excessive memory usage
#define MAX_QUEUE_SIZE 30

// Define macro switch to control whether to enable dump function, default is off
#define ENABLE_DUMP 0

struct _MediaVdec {
    GstElement element;
};

typedef struct _MediaVdecPrivate {
    AVCodecContext *codec_ctx;    // Codec context
    gboolean stop;                // Stop flag
    GstPad *sink_pad;             // Sink pad, used for receiving data
    GstPad *src_pad;              // Source pad, used for sending decoded data
    GMutex mutex;                 // Mutex for thread safety
    GCond cond;                   // Condition variable for thread synchronization
    GstCaps *caps;                // Negotiated capabilities
#if ENABLE_DUMP
    FILE *dump_file;              // File pointer for dumping video data
    FILE *es_dump_file;           // File pointer for dumping gstbuffer data sent to decode
#endif
} MediaVdecPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(MediaVdec, media_vdec, GST_TYPE_ELEMENT)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-h264, stream-format=(string)byte-stream; "
        "video/x-h265, stream-format=(string)byte-stream"
    )
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

// Declare chain function
static GstFlowReturn media_vdec_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstStateChangeReturn media_vdec_change_state(GstElement *element, GstStateChange transition);
static gboolean create_decoder(MediaVdecPrivate *priv, GstCaps *caps, AVRational time_base);
static const gchar* map_pix_fmt_to_string(enum AVPixelFormat pix_fmt);
static gboolean media_vdec_query_caps(GstPad *pad, GstObject *parent, GstQuery *query);
// Convert AVFrame to GstBuffer and push downstream
static gboolean push_decoded_buffer(MediaVdec *vdec, AVFrame *frame) {
    MediaVdecPrivate *priv = media_vdec_get_instance_private(vdec);
    GstBuffer *buffer;
    GstMapInfo map;
    guint size;

    // Calculate the size of the raw video data (assuming YUV420 format)
    size = frame->width * frame->height * 3 / 2;

    // Allocate a new GstBuffer
    buffer = gst_buffer_new_allocate(NULL, size, NULL);
    if (!buffer) {
        g_print("Failed to allocate GstBuffer\n");
        return FALSE;
    }

    // Map the buffer for writing
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        g_print("Failed to map GstBuffer\n");
        gst_buffer_unref(buffer);
        return FALSE;
    }
    g_print("AVFrame Info: width=%d, height=%d, format=%s, pts=%ld, dts=%ld, linesize[0]=%d, linesize[1]=%d, linesize[2]=%d\n",
            frame->width,
            frame->height,
            av_get_pix_fmt_name(frame->format),
            frame->pts,
            frame->pkt_dts,
            frame->linesize[0],
            frame->linesize[1],
            frame->linesize[2]);

    GST_BUFFER_PTS(buffer) = frame->pts;
    GST_BUFFER_DTS(buffer) = frame->pkt_dts;
    // Copy video data from AVFrame to GstBuffer
    for (int i = 0; i < frame->height; i++) {
        memcpy(map.data + i * frame->width, frame->data[0] + i * frame->linesize[0], frame->width); // Y
    }
    for (int i = 0; i < frame->height / 2; i++) {
        memcpy(map.data + frame->width * frame->height + i * frame->width / 2, frame->data[1] + i * frame->linesize[1], frame->width / 2); // U
    }
    for (int i = 0; i < frame->height / 2; i++) {
        memcpy(map.data + frame->width * frame->height * 5 / 4 + i * frame->width / 2, frame->data[2] + i * frame->linesize[2], frame->width / 2); // V
    }
    gst_buffer_unmap(buffer, &map);

    // Set buffer metadata (converted timestamps)

    // Push the buffer downstream
    GstFlowReturn ret = gst_pad_push(priv->src_pad, buffer);
    if (ret != GST_FLOW_OK) {
        g_print("Failed to push buffer downstream\n");
        gst_buffer_unref(buffer);
        return FALSE;
    }

#if ENABLE_DUMP
    // Dump AVFrame data to file
    for (int i = 0; i < frame->height; i++) {
        fwrite(frame->data[0] + i * frame->linesize[0], 1, frame->width, priv->dump_file); // Y
    }
    for (int i = 0; i < frame->height / 2; i++) {
        fwrite(frame->data[1] + i * frame->linesize[1], 1, frame->width / 2, priv->dump_file); // U
    }
    for (int i = 0; i < frame->height / 2; i++) {
        fwrite(frame->data[2] + i * frame->linesize[2], 1, frame->width / 2, priv->dump_file); // V
    }
#endif

    return TRUE;
}

// Chain function implementation
static GstFlowReturn media_vdec_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer) {
    MediaVdec *vdec = MEDIA_VDEC(parent);
    MediaVdecPrivate *priv = media_vdec_get_instance_private(vdec);
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    GstMapInfo map;

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        g_print("Failed to map GstBuffer\n");
        return GST_FLOW_ERROR;
    }

#if ENABLE_DUMP
    // Dump the gstbuffer data sent to decode to a file
    fwrite(map.data, 1, map.size, priv->es_dump_file);
#endif

    // Print the PTS and DTS of the frame before sending to decode
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    GstClockTime dts = GST_BUFFER_DTS(buffer);
    g_print("Frame before decode: PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts), GST_TIME_ARGS(dts));

    // Allocate and copy data to AVPacket
    packet = av_packet_alloc();
    if (!packet) {
        g_print("Failed to allocate AVPacket\n");
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    packet->data = g_malloc(map.size);
    memcpy(packet->data, map.data, map.size);
    packet->size = map.size;

    // Correctly rescale PTS and DTS to avcodec's time base
    if (pts != GST_CLOCK_TIME_NONE) {
        packet->pts = pts;
    }


    if (dts != GST_CLOCK_TIME_NONE) {
        packet->dts = dts;
    }

    g_print("media_vdec_chain: Packet PTS: %" PRId64 ", DTS: %" PRId64 "\n", packet->pts, packet->dts);

    gst_buffer_unmap(buffer, &map);

    // Get caps if not already obtained
    if (!priv->caps) {
        priv->caps = gst_pad_get_current_caps(pad);
    }

    // Send packet to decoder
    if (avcodec_send_packet(priv->codec_ctx, packet) < 0) {
        g_print("Failed to send packet to decoder\n");
        av_packet_free(&packet);
        return GST_FLOW_ERROR;
    }

    av_packet_free(&packet);

    // Receive decoded frame
    frame = av_frame_alloc();
    if (!frame) {
        g_print("Failed to allocate AVFrame\n");
        return GST_FLOW_ERROR;
    }

    while (avcodec_receive_frame(priv->codec_ctx, frame) == 0) {
        // Push decoded frame downstream
        if (!push_decoded_buffer(vdec, frame)) {
            g_print("Failed to push decoded frame downstream\n");
            av_frame_free(&frame);
            return GST_FLOW_ERROR;
        }
    }

    av_frame_free(&frame);
    gst_buffer_unref(buffer);
    return GST_FLOW_OK;
}

// Handle sink pad events
static gboolean media_vdec_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret;
    MediaVdec *vdec = MEDIA_VDEC(parent);
    MediaVdecPrivate *priv = media_vdec_get_instance_private(vdec);

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            g_print("media_vdec_sink_event Caps event received with caps: %s\n", gst_caps_to_string(caps));

            // Extract time_base (assuming time_base information is added in caps)
            gint num, den;
            if (gst_structure_get_fraction(gst_caps_get_structure(caps, 0), "time-base", &num, &den)) {
                AVRational time_base = { .num = num, .den = den };
                // Create decoder and pass time_base
                if (!create_decoder(priv, caps, time_base)) {
                    g_print("Failed to create decoder with provided time_base\n");
                    return FALSE;
                }
            } else {
                g_print("time_base not found in caps\n");
                return FALSE;
            }

            priv->caps = gst_caps_ref(caps);

            // Extract width, height, framerate, and format from sink pad caps
            gint width, height;
            gint framerate_num, framerate_den;
            const gchar *format;
            gst_structure_get_int(gst_caps_get_structure(caps, 0), "width", &width);
            gst_structure_get_int(gst_caps_get_structure(caps, 0), "height", &height);
            gst_structure_get_fraction(gst_caps_get_structure(caps, 0), "framerate", &framerate_num, &framerate_den);
            format = gst_structure_get_string(gst_caps_get_structure(caps, 0), "format");

            // Set caps for src pad
            GstCaps *video_caps = gst_caps_new_simple(
                "video/x-raw",
                "width", G_TYPE_INT, width,
                "height", G_TYPE_INT, height,
                "format", G_TYPE_STRING, format,
                "framerate", GST_TYPE_FRACTION, framerate_num, framerate_den,
                NULL
            );

            gst_pad_set_caps(priv->src_pad, video_caps);
            gst_caps_unref(video_caps);

            // Push caps event downstream
            GstEvent *new_event = gst_event_new_caps(video_caps);
            if (!gst_pad_push_event(priv->src_pad, new_event)) {
                g_print("Failed to push caps event downstream\n");
                return FALSE;
            }

            break;
        }
        default:
            break;
    }

    ret = gst_pad_event_default(pad, parent, event);
    return ret;
}

// Handle src pad events
static gboolean media_vdec_src_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret;
    MediaVdec *vdec = MEDIA_VDEC(parent);

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_EOS:
            g_print("media_vdec_src_event: Received EOS event\n");
            break;
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            g_print("media_vdec_src_event: caps event, caps: %s\n", gst_caps_to_string(caps));
                       break;
        }
        default:
            break;
    }

    ret = gst_pad_event_default(pad, parent, event);
    return ret;
}

// Create decoder based on caps
static gboolean create_decoder(MediaVdecPrivate *priv, GstCaps *caps, AVRational time_base) {
    const AVCodec *codec = NULL;

    if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-h264"))) {
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    } else if (gst_caps_can_intersect(caps, gst_caps_from_string("video/x-h265"))) {
        codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    } else {
        g_print("Unsupported caps format\n");
        return FALSE;
    }

    if (!codec) {
        g_print("Decoder not found\n");
        return FALSE;
    }

    priv->codec_ctx = avcodec_alloc_context3(codec);
    if (!priv->codec_ctx) {
        g_print("Failed to allocate AVCodecContext\n");
        return FALSE;
    }

    // Set time_base
    priv->codec_ctx->time_base = time_base;

    if (avcodec_open2(priv->codec_ctx, codec, NULL) < 0) {
        g_print("Failed to open decoder\n");
        avcodec_free_context(&priv->codec_ctx);
        return FALSE;
    }

#if ENABLE_DUMP
    // Open dump file
    priv->dump_file = fopen("/Users/lizhen/Downloads/dump_video.yuv", "wb");
    if (!priv->dump_file) {
        g_print("Failed to open dump file\n");
        avcodec_free_context(&priv->codec_ctx);
        return FALSE;
    }

    // Open es dump file
    priv->es_dump_file = fopen("/Users/lizhen/Downloads/dump_video.es", "wb");
    if (!priv->es_dump_file) {
        g_print("Failed to open es dump file\n");
        fclose(priv->dump_file);
        avcodec_free_context(&priv->codec_ctx);
        return FALSE;
    }
#endif

    g_print("Decoder created with time_base: %d/%d\n", priv->codec_ctx->time_base.num, priv->codec_ctx->time_base.den);
    return TRUE;
}

// Class initialization function
static void media_vdec_class_init(MediaVdecClass *klass) {
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

    // Set element metadata
    gst_element_class_set_static_metadata(gstelement_class,
        "Media Video Decoder",
        "Decoder/Video",
        "Decode video frames using avcodec, supporting multiple encoding formats",
        "Your Name <youremail@example.com>"
    );

    // Add pad templates to the element class
    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_template));

    // Set state change function
    gstelement_class->change_state = media_vdec_change_state;
}

// Instance initialization function
static void media_vdec_init(MediaVdec *vdec) {
    MediaVdecPrivate *priv = media_vdec_get_instance_private(vdec);
    g_print("media_vdec_init: Initializing MediaVdec instance\n");

    // Initialize private member variables
    priv->codec_ctx = NULL;
    priv->stop = FALSE;
    priv->caps = NULL;
#if ENABLE_DUMP
    priv->dump_file = NULL;
    priv->es_dump_file = NULL;
#endif

    // Create and add sink pad
    priv->sink_pad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(priv->sink_pad, GST_DEBUG_FUNCPTR(media_vdec_chain));
    gst_pad_set_event_function(priv->sink_pad, GST_DEBUG_FUNCPTR(media_vdec_sink_event));
    gst_pad_set_query_function(priv->sink_pad, GST_DEBUG_FUNCPTR(media_vdec_query_caps));
    gst_element_add_pad(GST_ELEMENT(vdec), priv->sink_pad);

    // Create and add src pad
    priv->src_pad = gst_pad_new_from_static_template(&src_template, "src");
    gst_pad_set_event_function(priv->src_pad, GST_DEBUG_FUNCPTR(media_vdec_src_event));
    // gst_pad_set_query_function(priv->src_pad, GST_DEBUG_FUNCPTR(media_vdec_query_caps));
    gst_element_add_pad(GST_ELEMENT(vdec), priv->src_pad);

    // Initialize mutex and condition variables
    g_mutex_init(&priv->mutex);
    g_cond_init(&priv->cond);
}

// Handle state changes
static GstStateChangeReturn media_vdec_change_state(GstElement *element, GstStateChange transition) {
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    MediaVdec *vdec = MEDIA_VDEC(element);
    MediaVdecPrivate *priv = media_vdec_get_instance_private(vdec);

    g_print("media_vdec_change_state: transition: %d\n", transition);
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            g_print("State changed from NULL to READY\n");
            // Initialize or configure elements if needed
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            g_print("State changed from READY to PAUSED\n");
            // Handle transition from READY to PAUSED if needed
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            g_print("State changed from PAUSED to PLAYING\n");
            // Handle transition from PAUSED to PLAYING if needed
            break;
        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(media_vdec_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_READY_TO_NULL:
            g_print("State changed from READY to NULL\n");
            // Clean up resources if needed
            priv->stop = TRUE;
            g_cond_signal(&priv->cond);
#if ENABLE_DUMP
            if (priv->dump_file) {
                fclose(priv->dump_file);
                priv->dump_file = NULL;
            }
            if (priv->es_dump_file) {
                fclose(priv->es_dump_file);
                priv->es_dump_file = NULL;
            }
#endif
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            g_print("State changed from PAUSED to READY\n");
            // Handle transition from PAUSED to READY if needed
            break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            g_print("State changed from PLAYING to PAUSED\n");
            // Handle transition from PLAYING to PAUSED if needed
            break;
        default:
            break;
    }

    return ret;
}

// Query caps function
static gboolean media_vdec_query_caps(GstPad *pad, GstObject *parent, GstQuery *query) {
    MediaVdec *vdec = MEDIA_VDEC(parent);
    MediaVdecPrivate *priv = media_vdec_get_instance_private(vdec);
    gboolean ret = FALSE;

    switch (GST_QUERY_TYPE(query)) {
        case GST_QUERY_CAPS: {
            GstCaps *filter;
            gst_query_parse_caps(query, &filter);
            g_print("media_vdec_query_caps: Querying caps for pad: %s, filter: %s\n", GST_PAD_NAME(pad), gst_caps_to_string(filter));

            GstCaps *caps = gst_pad_get_pad_template_caps(pad);
            if (filter) {
                GstCaps *intersection = gst_caps_intersect(caps, filter);
                gst_caps_unref(caps);
                caps = intersection;
            }
            g_print("media_vdec_query_caps: Querying caps for pad: %s, caps: %s\n", GST_PAD_NAME(pad), gst_caps_to_string(caps));
            gst_query_set_caps_result(query, caps);
            gst_caps_unref(caps);
            ret = TRUE;
            break;
        }
        default:
            ret = gst_pad_query_default(pad, parent, query);
            break;
    }

    return ret;
}

// Map AVPixelFormat to string
static const gchar* map_pix_fmt_to_string(enum AVPixelFormat pix_fmt) {
    switch (pix_fmt) {
        case AV_PIX_FMT_YUV420P:
            return "I420";
        case AV_PIX_FMT_YUYV422:
            return "YUY2";
        case AV_PIX_FMT_YUV422P:
            return "Y42B";
        case AV_PIX_FMT_YUV444P:
            return "Y444";
        case AV_PIX_FMT_YUV410P:
            return "YUV9";
        case AV_PIX_FMT_YUV411P:
            return "Y41B";
        case AV_PIX_FMT_NV12:
            return "NV12";
        case AV_PIX_FMT_NV21:
            return "NV21";
        case AV_PIX_FMT_YUV420P10BE:
            return "I420_10BE";
        case AV_PIX_FMT_YUV420P10LE:
            return "I420_10LE";
        case AV_PIX_FMT_YUV422P10BE:
            return "I422_10BE";
        case AV_PIX_FMT_YUV422P10LE:
            return "I422_10LE";
        case AV_PIX_FMT_YUV444P10BE:
            return "Y444_10BE";
        case AV_PIX_FMT_YUV444P10LE:
            return "Y444_10LE";
        case AV_PIX_FMT_YUV420P12BE:
            return "I420_12BE";
        case AV_PIX_FMT_YUV420P12LE:
            return "I420_12LE";
        case AV_PIX_FMT_YUV422P12BE:
            return "I422_12BE";
        case AV_PIX_FMT_YUV422P12LE:
            return "I422_12LE";
        case AV_PIX_FMT_YUV444P12BE:
            return "Y444_12BE";
        case AV_PIX_FMT_YUV444P12LE:
            return "Y444_12LE";
        case AV_PIX_FMT_P010LE:
            return "P010_10LE";
        case AV_PIX_FMT_VUYA:
            return "VUYA";
        case AV_PIX_FMT_P012LE:
            return "P012_LE";
        case AV_PIX_FMT_YUV422P16LE:
            return "Y212_LE";
        case AV_PIX_FMT_YUV444P16LE:
            return "Y412_LE";
        default:
            return "unknown";
    }
}

// Plugin initialization function
gboolean media_vdec_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "media_vdec", GST_RANK_NONE, MEDIA_TYPE_VDEC);
}

// Define plugin
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    media_vdec,
    "Plugin for decoding video frames using avcodec",
    media_vdec_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)

