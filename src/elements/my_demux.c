#define PACKAGE "my_plugin"
#include "my_demux.h"
#include <gst/gst.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h> // 添加此行以包含 av_get_pix_fmt_name 的声明
#include "media_parser.h"

G_DEFINE_TYPE(MyDemux, my_demux, GST_TYPE_ELEMENT)

static GstStateChangeReturn my_demux_change_state(GstElement *element, GstStateChange transition);
static void my_demux_loop(MyDemux *demux);
static GstFlowReturn my_demux_push_data(MyDemux *demux);
static const gchar* map_pix_fmt_to_string(enum AVPixelFormat pix_fmt);

// 定义视频的动态 Pad 模板 (音频流被忽略)
static GstStaticPadTemplate video_src_template_h264 = GST_STATIC_PAD_TEMPLATE(
    "video_src_%u", GST_PAD_SRC, GST_PAD_SOMETIMES,
    GST_STATIC_CAPS("video/x-h264"));

static GstStaticPadTemplate video_src_template_h265 = GST_STATIC_PAD_TEMPLATE(
    "video_src_%u", GST_PAD_SRC, GST_PAD_SOMETIMES,
    GST_STATIC_CAPS("video/x-h265"));

enum {
    PROP_0,
    PROP_LOCATION,
};

// 设置和获取属性的函数
static void my_demux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void my_demux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void my_demux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    MyDemux *demux = MY_DEMUX(object);

    switch (prop_id) {
        case PROP_LOCATION:
            if (demux->location) {
                g_free(demux->location);
            }
            demux->location = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void my_demux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    MyDemux *demux = MY_DEMUX(object);

    switch (prop_id) {
        case PROP_LOCATION:
            g_value_set_string(value, demux->location);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void my_demux_class_init(MyDemuxClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = my_demux_set_property;
    gobject_class->get_property = my_demux_get_property;

    // 注册 location 属性
    g_object_class_install_property(gobject_class, PROP_LOCATION,
        g_param_spec_string("location", "Location", "File path to open", NULL,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(element_class,
        "MyDemux", "Demuxer",
        "Custom FFmpeg-based Demuxer",
        "Your Name <your.email@example.com>");

    // 添加视频的动态 pad 模板
    gst_element_class_add_static_pad_template(element_class, &video_src_template_h264);
    gst_element_class_add_static_pad_template(element_class, &video_src_template_h265);

    element_class->change_state = GST_DEBUG_FUNCPTR(my_demux_change_state);
}

static void my_demux_init(MyDemux *demux) {
    demux->fmt_ctx = NULL;
    demux->started = FALSE;
    demux->video_stream_idx = -1;
    demux->video_src_pad = NULL;
    demux->task = NULL;
    demux->task_lock = NULL;
    demux->location = NULL;
}

static void my_demux_finalize(GObject *object) {
    MyDemux *demux = MY_DEMUX(object);

    if (demux->task) {
        gst_task_stop(demux->task);
        gst_task_join(demux->task);
        g_object_unref(demux->task);
        demux->task = NULL;
    }

    if (demux->task_lock) {
        g_rec_mutex_clear(demux->task_lock);
        g_free(demux->task_lock);
        demux->task_lock = NULL;
    }

    if (demux->location) {
        g_free(demux->location);
        demux->location = NULL;
    }

    if (demux->fmt_ctx) {
        avformat_close_input(&demux->fmt_ctx);
        demux->fmt_ctx = NULL;
    }

    G_OBJECT_CLASS(my_demux_parent_class)->finalize(object);
}

// 提取 codec_data 并传递给下游元素
static gboolean my_demux_start(MyDemux *demux) {
    if (!demux->location) {
        GST_ERROR_OBJECT(demux, "No file location specified.");
        return FALSE;
    }
    AVDictionary *options = NULL;
    // 设置探测数据量
    av_dict_set(&options, "probesize", "4000", 0);        // 设置探测数据量为 500KB
    av_dict_set(&options, "analyzeduration", "10000", 0); // 设置探测时长为 1秒


    GTimer *open_timer = g_timer_new();
    g_timer_start(open_timer);

    if (avformat_open_input(&demux->fmt_ctx, demux->location, NULL, &options) < 0) {
        GST_ERROR_OBJECT(demux, "Failed to open file at location: %s", demux->location);
        g_timer_destroy(open_timer);
        return FALSE;
    }

    g_timer_stop(open_timer);
    gdouble open_elapsed_time = g_timer_elapsed(open_timer, NULL);
    g_print("Open input file execution time: %f seconds\n", open_elapsed_time);
    g_timer_destroy(open_timer);
    av_dict_free(&options);
    // demux->fmt_ctx->probesize = 4 * 1024; // 设置探测大小为32KB
    // demux->fmt_ctx->max_analyze_duration = 10000; // 设置最大分析时长为5秒

    //设置所有流的discard为AVDISCARD_ALL以减少探测时间
    for (unsigned int i = 0; i < demux->fmt_ctx->nb_streams; i++) {
        demux->fmt_ctx->streams[i]->discard = AVDISCARD_NONKEY;
    }

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    if (avformat_find_stream_info(demux->fmt_ctx, NULL) < 0) {
        GST_ERROR_OBJECT(demux, "Failed to find stream info.");
        g_timer_destroy(timer);
        return FALSE;
    }

    g_timer_stop(timer);
    gdouble elapsed_time = g_timer_elapsed(timer, NULL);
    g_print("Execution time: %f seconds\n", elapsed_time);
    g_timer_destroy(timer);

    // 恢复所有流的discard为AVDISCARD_DEFAULT
    for (unsigned int i = 0; i < demux->fmt_ctx->nb_streams; i++) {
        demux->fmt_ctx->streams[i]->discard = AVDISCARD_DEFAULT;
    }

    // 只处理视频流
    for (gint i = 0; i < demux->fmt_ctx->nb_streams; i++) {
        AVStream *stream = demux->fmt_ctx->streams[i];
        gchar *stream_id = g_strdup_printf("stream-%d", i);

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && demux->video_stream_idx == -1) {
            demux->video_stream_idx = i;

            if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
                demux->video_src_pad = gst_pad_new_from_static_template(&video_src_template_h264, "video_src_%u");
            } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
                demux->video_src_pad = gst_pad_new_from_static_template(&video_src_template_h265, "video_src_%u");
            } else {
                g_free(stream_id);
                continue;
            }

            // 激活 src pad
            gst_pad_set_active(demux->video_src_pad, TRUE);

            // 设置 caps 和 codec_data
            GstCaps *video_caps = NULL;

            const gchar *stream_format = NULL;
            if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
                stream_format = "avc";
            } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
                stream_format = "hvc1";
            }

            // 获取像素格式并映射到 GStreamer 支持的格式
            const gchar *pixel_format = map_pix_fmt_to_string(stream->codecpar->format);

            if (stream->codecpar->extradata_size > 0 && stream->codecpar->extradata != NULL) {
                GstBuffer *codec_data = gst_buffer_new_allocate(NULL, stream->codecpar->extradata_size, NULL);
                gst_buffer_fill(codec_data, 0, stream->codecpar->extradata, stream->codecpar->extradata_size);

                if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
                    video_caps = gst_caps_new_simple("video/x-h264",
                                                     "stream-format", G_TYPE_STRING, stream_format,
                                                     "alignment", G_TYPE_STRING, "au",
                                                     "width", G_TYPE_INT, stream->codecpar->width,
                                                     "height", G_TYPE_INT, stream->codecpar->height,
                                                     "codec_data", GST_TYPE_BUFFER, codec_data,
                                                     "format", G_TYPE_STRING, pixel_format,
                                                     "time-base", GST_TYPE_FRACTION, stream->time_base.num, stream->time_base.den,
                                                     NULL);
                } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
                    video_caps = gst_caps_new_simple("video/x-h265",
                                                     "stream-format", G_TYPE_STRING, stream_format,
                                                     "alignment", G_TYPE_STRING, "au",
                                                     "width", G_TYPE_INT, stream->codecpar->width,
                                                     "height", G_TYPE_INT, stream->codecpar->height,
                                                     "codec_data", GST_TYPE_BUFFER, codec_data,
                                                     "format", G_TYPE_STRING, pixel_format,
                                                     "time-base", GST_TYPE_FRACTION, stream->time_base.num, stream->time_base.den,
                                                     NULL);
                }
                gst_buffer_unref(codec_data);
            } else {
                if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
                    video_caps = gst_caps_new_simple("video/x-h264",
                                                     "stream-format", G_TYPE_STRING, stream_format,
                                                     "alignment", G_TYPE_STRING, "au",
                                                     "width", G_TYPE_INT, stream->codecpar->width,
                                                     "height", G_TYPE_INT, stream->codecpar->height,
                                                     "format", G_TYPE_STRING, pixel_format,
                                                     "time-base", GST_TYPE_FRACTION, stream->time_base.num, stream->time_base.den,
                                                     NULL);
                } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
                    video_caps = gst_caps_new_simple("video/x-h265",
                                                     "stream-format", G_TYPE_STRING, stream_format,
                                                     "alignment", G_TYPE_STRING, "au",
                                                     "width", G_TYPE_INT, stream->codecpar->width,
                                                     "height", G_TYPE_INT, stream->codecpar->height,
                                                     "format", G_TYPE_STRING, pixel_format,
                                                     "time-base", GST_TYPE_FRACTION, stream->time_base.num, stream->time_base.den,
                                                     NULL);
                }
            }

            // 发送 stream-start 事件
            GstEvent *stream_start_event = gst_event_new_stream_start(stream_id);
            gst_event_set_group_id(stream_start_event, gst_util_group_id_next());
            gst_pad_push_event(demux->video_src_pad, stream_start_event);

            // 设置 caps
            gst_pad_set_caps(demux->video_src_pad, video_caps);
            gst_caps_unref(video_caps);

            // 发送 segment 事件
            GstSegment segment;
            gst_segment_init(&segment, GST_FORMAT_TIME);
            segment.start = 0;
            segment.time = 0;
            GstEvent *segment_event = gst_event_new_segment(&segment);
            gst_pad_push_event(demux->video_src_pad, segment_event);

            g_free(stream_id);  // 释放 stream_id

            gst_element_add_pad(GST_ELEMENT(demux), demux->video_src_pad);

            break; // 找到一个视频流即，跳出循环
        }

        g_free(stream_id);
    }

    if (demux->video_stream_idx == -1) {
        GST_ERROR_OBJECT(demux, "No valid video streams found.");
        return FALSE;
    }

    demux->started = TRUE;

    // 在这里初始化任务
    if (!demux->task) {
        demux->task = gst_task_new((GstTaskFunction)my_demux_loop, demux, NULL);
        demux->task_lock = g_new(GRecMutex, 1);
        g_rec_mutex_init(demux->task_lock);
        gst_task_set_lock(demux->task, demux->task_lock);
    }

    return TRUE;
}

static gboolean my_demux_stop(MyDemux *demux) {
    demux->started = FALSE;

    if (demux->task) {
        gst_task_stop(demux->task);
        gst_task_join(demux->task);
    }

    if (demux->fmt_ctx) {
        avformat_close_input(&demux->fmt_ctx);
        demux->fmt_ctx = NULL;
    }

    return TRUE;
}

static GstFlowReturn my_demux_push_data(MyDemux *demux) {
    AVPacket pkt;

    // 获取当前处理的 AVStream
    AVStream *stream = demux->fmt_ctx->streams[demux->video_stream_idx];

    // 读取一帧数据
    if (av_read_frame(demux->fmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == demux->video_stream_idx) {
            GstBuffer *buffer = gst_buffer_new_allocate(NULL, pkt.size, NULL);
            gst_buffer_fill(buffer, 0, pkt.data, pkt.size);
            g_print("my_demux_push_data: Packet PTS: %" PRId64 ", DTS: %" PRId64 "\n", pkt.pts, pkt.dts);
            // 使用 av_rescale_q 将时间戳转换为 GstClockTime
            if (pkt.pts != AV_NOPTS_VALUE) {
                GST_BUFFER_PTS(buffer) = av_rescale_q(pkt.pts, stream->time_base, (AVRational){1, GST_SECOND});
            } else {
                GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;
            }

            if (pkt.dts != AV_NOPTS_VALUE) {
                GST_BUFFER_DTS(buffer) = av_rescale_q(pkt.dts, stream->time_base, (AVRational){1, GST_SECOND});
            } else {
                GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
            }

            if (pkt.duration > 0) {
                GST_BUFFER_DURATION(buffer) = av_rescale_q(pkt.duration, stream->time_base, (AVRational){1, GST_SECOND});
            } else {
                GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
            }

            g_print("my_demux: PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "\n", 
                    GST_TIME_ARGS(GST_BUFFER_PTS(buffer)), GST_TIME_ARGS(GST_BUFFER_DTS(buffer)));

            GstFlowReturn ret = gst_pad_push(demux->video_src_pad, buffer);
            if (ret != GST_FLOW_OK) {
                g_printerr("Failed to push buffer to pad, flow return: %d\n", ret);
            }
            av_packet_unref(&pkt);
            return ret;
        }

        av_packet_unref(&pkt);
    } else {
        // 读取结束，发送 EOS
        gst_pad_push_event(demux->video_src_pad, gst_event_new_eos());
        return GST_FLOW_EOS;
    }

    return GST_FLOW_OK;
}

static void my_demux_loop(MyDemux *demux) {
    while (demux->started) {
        GstFlowReturn ret = my_demux_push_data(demux);
        if (ret != GST_FLOW_OK) {
            // 处理错误或 EOS
            break;
        }
        // 控制读取速度
        g_usleep(1000);
    }
}

static GstStateChangeReturn my_demux_change_state(GstElement *element, GstStateChange transition) {
    g_print("my_demux_change_state: Entering function.\n");
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    MyDemux *demux = MY_DEMUX(element);

    g_print("my_demux_change_state: transition: %d\n", transition);

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            g_print("my_demux_change_state: Switching from NULL to READY\n");
            // 初始化 FFmpeg 库（如果尚未初始化）
            // avformat_network_init();
            break;

        case GST_STATE_CHANGE_READY_TO_PAUSED:
            g_print("my_demux_change_state: Switching from READY to PAUSED\n");
            if (!my_demux_start(demux)) {
                g_print("my_demux_change_state: Failed to start demuxer.\n");
                return GST_STATE_CHANGE_FAILURE;
            }
            // 在 PAUSED 状态启动任务，以便在 PAUSED 状态下推送数据
            gst_task_start(demux->task);
            break;

        case GST_STATE_CHANGE_PAUSED_TO_READY:
            g_print("my_demux_change_state: Switching from PAUSED to READY\n");
            my_demux_stop(demux);
            break;

        case GST_STATE_CHANGE_READY_TO_NULL:
            g_print("my_demux_change_state: Switching from READY to NULL\n");
            break;

        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            g_print("my_demux_change_state: Switching from PLAYING to PAUSED\n");
            break;

        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            g_print("my_demux_change_state: Switching from PAUSED to PLAYING\n");
            break;

        default:
            g_print("my_demux_change_state: Unhandled state change\n");
            break;
    }

    ret = GST_ELEMENT_CLASS(my_demux_parent_class)->change_state(element, transition);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_print("my_demux_change_state: State change failed.\n");
    } else {
        g_print("my_demux_change_state: State change successful.\n");
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

gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "mydemux", GST_RANK_NONE, MY_TYPE_DEMUX);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mydemux,
    "Custom FFmpeg-based Demuxer",
    plugin_init,
    "1.0",
    "LGPL",
    PACKAGE,
    "http://gstreamer.net/"
)