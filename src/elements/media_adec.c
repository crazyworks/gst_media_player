#define PACKAGE "my_plugin"
#include "media_adec.h"
#include <gst/gst.h>
#include <libavcodec/avcodec.h>
#include <glib.h>
#include <pthread.h>

typedef struct _MediaAdecPrivate {
    AVCodecContext *codec_ctx;    // Codec context
    gboolean stop;                // Stop flag
    GstPad *sink_pad;             // Sink pad
    GstPad *src_pad;              // Source pad
    GstCaps *caps;                // Negotiated capabilities
} MediaAdecPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(MediaAdec, media_adec, GST_TYPE_ELEMENT)

// Forward declaration of chain function
static GstFlowReturn media_adec_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean media_adec_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);

// Define static pad templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/raw")
);

// Function to convert AVFrame to GstBuffer and push it downstream
static gboolean push_decoded_buffer(MediaAdec *adec, AVFrame *frame) {
    MediaAdecPrivate *priv = media_adec_get_instance_private(adec);
    GstBuffer *buffer;
    GstMapInfo map;
    guint size;

    // Calculate the size of the raw audio data
    size = frame->nb_samples * av_get_bytes_per_sample(priv->codec_ctx->sample_fmt) * priv->codec_ctx->ch_layout.nb_channels;

    // Allocate a new GstBuffer with the required size
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

    frame->pts = GST_BUFFER_PTS(buffer);
    frame->pkt_dts = GST_BUFFER_DTS(buffer);
    // Copy raw audio data from AVFrame to GstBuffer
    memcpy(map.data, frame->data[0], size);
    gst_buffer_unmap(buffer, &map);

    // Push the buffer downstream
    GstFlowReturn ret = gst_pad_push(priv->src_pad, buffer);
    if (ret != GST_FLOW_OK) {
        g_print("Failed to push buffer downstream\n");
        gst_buffer_unref(buffer);
        return FALSE;
    }

    return TRUE;
}

// Chain function to handle incoming buffers
static GstFlowReturn media_adec_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer) {
    MediaAdec *adec = MEDIA_ADEC(parent);
    MediaAdecPrivate *priv = media_adec_get_instance_private(adec);
    AVFrame *frame = NULL;
    AVPacket *packet = NULL;

    if (!priv->codec_ctx) {
        g_print("Codec context not initialized\n");
        return GST_FLOW_ERROR;
    }

    frame = av_frame_alloc();
    if (!frame) {
        g_print("Failed to allocate AVFrame\n");
        return GST_FLOW_ERROR;
    }

    packet = av_packet_alloc();
    if (!packet) {
        g_print("Failed to allocate AVPacket\n");
        av_frame_free(&frame);
        return GST_FLOW_ERROR;
    }

    // Convert GstBuffer to AVPacket
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        g_print("Failed to map GstBuffer\n");
        gst_buffer_unref(buffer);
        av_packet_free(&packet);
        av_frame_free(&frame);
        return GST_FLOW_ERROR;
    }
    packet->pts = GST_BUFFER_PTS(buffer);
    packet->dts = GST_BUFFER_DTS(buffer);

    packet->data = g_malloc(map.size);
    memcpy(packet->data, map.data, map.size);
    packet->size = map.size;
    gst_buffer_unmap(buffer, &map);

    // Send packet to decoder
    if (avcodec_send_packet(priv->codec_ctx, packet) < 0) {
        g_print("Failed to send packet to decoder\n");
        av_packet_free(&packet);
        av_frame_free(&frame);
        return GST_FLOW_ERROR;
    }

    av_packet_free(&packet);

    // Receive frames from decoder
    while (avcodec_receive_frame(priv->codec_ctx, frame) == 0) {
        // Push decoded frame downstream
        if (!push_decoded_buffer(adec, frame)) {
            g_print("Failed to push decoded buffer\n");
            av_frame_free(&frame);
            return GST_FLOW_ERROR;
        }
    }

    av_frame_free(&frame);
    gst_buffer_unref(buffer);
    return GST_FLOW_OK;
}

// Event handler function to handle caps event
static gboolean media_adec_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    MediaAdec *adec = MEDIA_ADEC(parent);
    MediaAdecPrivate *priv = media_adec_get_instance_private(adec);

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            priv->caps = gst_caps_ref(caps);

            // Determine codec type based on negotiated caps
            enum AVCodecID codec_type = AV_CODEC_ID_NONE;
            const gchar *media_type = gst_structure_get_name(gst_caps_get_structure(priv->caps, 0));
            if (g_str_has_suffix(media_type, "aac")) {
                codec_type = AV_CODEC_ID_AAC;
            } else if (g_str_has_suffix(media_type, "mp3")) {
                codec_type = AV_CODEC_ID_MP3;
            }
            // Add more codec types as needed

            // Initialize decoder based on codec type
            const AVCodec *codec = NULL;
            switch (codec_type) {
                case AV_CODEC_ID_AAC:
                    codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
                    break;
                case AV_CODEC_ID_MP3:
                    codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
                    break;
                default:
                    g_print("Unsupported codec type\n");
                    return FALSE;
            }

            if (!codec) {
                g_print("Decoder not found for the specified codec\n");
                return FALSE;
            }

            priv->codec_ctx = avcodec_alloc_context3(codec);
            if (!priv->codec_ctx) {
                g_print("Failed to allocate codec context\n");
                return FALSE;
            }

            if (avcodec_open2(priv->codec_ctx, codec, NULL) < 0) {
                g_print("Failed to open decoder\n");
                avcodec_free_context(&priv->codec_ctx);
                return FALSE;
            }

            // 修改格式并发送到下游
            GstCaps *new_caps = gst_caps_new_simple("audio/x-raw",
                                                    "format", G_TYPE_STRING, "S16LE",
                                                    "rate", G_TYPE_INT, priv->codec_ctx->sample_rate,
                                                    "channels", G_TYPE_INT, priv->codec_ctx->ch_layout.nb_channels,
                                                    NULL);
            gst_pad_set_caps(priv->src_pad, new_caps);
            gst_caps_unref(new_caps);

            break;
        }
        default:
            break;
    }

    return gst_pad_event_default(pad, parent, event);
}

// Clean up function
static void media_adec_finalize(GObject *object) {
    MediaAdec *adec = MEDIA_ADEC(object);
    MediaAdecPrivate *priv = media_adec_get_instance_private(adec);

    if (priv->codec_ctx) {
        avcodec_free_context(&priv->codec_ctx);
    }

    G_OBJECT_CLASS(media_adec_parent_class)->finalize(object);
}

// Class initialization function
static void media_adec_class_init(MediaAdecClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = media_adec_finalize;

    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    gst_element_class_set_static_metadata(gstelement_class,
        "My Audio Decoder",
        "Codec/Decoder",
        "Decodes various audio streams based on codec type",
        "Your Name <youremail@example.com>");

    // Add sink pad template
    GstPadTemplate *sink_pad_template_ptr = gst_static_pad_template_get(&sink_template);
    gst_element_class_add_pad_template(gstelement_class, sink_pad_template_ptr);

    // Add src pad template
    GstPadTemplate *src_pad_template_ptr = gst_static_pad_template_get(&src_template);
    gst_element_class_add_pad_template(gstelement_class, src_pad_template_ptr);
}

// Instance initialization function
static void media_adec_init(MediaAdec *adec) {
    MediaAdecPrivate *priv = media_adec_get_instance_private(adec);

    priv->codec_ctx = NULL;
    priv->stop = FALSE;
    priv->caps = NULL;

    priv->sink_pad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(priv->sink_pad, GST_DEBUG_FUNCPTR(media_adec_chain));
    gst_pad_set_event_function(priv->sink_pad, GST_DEBUG_FUNCPTR(media_adec_sink_event));
    gst_element_add_pad(GST_ELEMENT(adec), priv->sink_pad);

    priv->src_pad = gst_pad_new_from_static_template(&src_template, "src");
    gst_element_add_pad(GST_ELEMENT(adec), priv->src_pad);
}

// Plugin initialization function
gboolean media_adec_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "media_adec", GST_RANK_NONE, MEDIA_TYPE_ADEC);
}

// Define plugin
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    media_adec,
    "My Audio Decoder Plugin",
    media_adec_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://example.com/"
)
