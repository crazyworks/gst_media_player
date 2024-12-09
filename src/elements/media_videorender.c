#define PACKAGE "my_plugin"
#include "media_videorender.h"
#include <gst/video/video.h>
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdbool.h>
#include <glib.h>

// 定义 sink pad 模板
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw, "
        "format=(string){ YV12, IYUV, NV12, NV21, YUY2, UYVY, RGB24, ARGB8888, RGBA8888, ABGR8888, BGRA8888, RGB565, BGR565, RGB332 }, "
        "width=(int)800, "
        "height=(int)600, "
        "framerate=(fraction)60/1"
    )
);

typedef struct _MediaVideoRenderPrivate {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture; // 用于渲染视频帧的纹理
    pthread_t render_thread; // 渲染线程
    pthread_mutex_t queue_mutex; // 队列访问的互斥锁
    pthread_cond_t queue_cond; // 用于队列的条件变量
    GQueue *frame_queue; // 帧队列
    bool stop_rendering; // 停止渲染线程的标志
    GstClockTime base_time; // 基准时间，用于音画同步
} MediaVideoRenderPrivate;

struct _MediaVideoRender {
    GstElement parent;
};

G_DEFINE_TYPE_WITH_PRIVATE(MediaVideoRender, media_videorender, GST_TYPE_ELEMENT)

// 渲染线程函数
static void* render_loop(void *data) {
    MediaVideoRender *render = (MediaVideoRender *)data;
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);

    while (!priv->stop_rendering) {
        pthread_mutex_lock(&priv->queue_mutex);

        // 等待直到有帧可用或停止标志置为真
        while (g_queue_is_empty(priv->frame_queue) && !priv->stop_rendering) {
            pthread_cond_wait(&priv->queue_cond, &priv->queue_mutex);
        }

        if (priv->stop_rendering) {
            pthread_mutex_unlock(&priv->queue_mutex);
            break;
        }

        // 从队列中取出帧
        GstBuffer *buffer = g_queue_pop_head(priv->frame_queue);
        pthread_mutex_unlock(&priv->queue_mutex);

        // 渲染帧
        if (buffer) {
            GstMapInfo map;
            if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                SDL_UpdateTexture(priv->texture, NULL, map.data, 800 * 3 / 2); // 假设YUV格式
                gst_buffer_unmap(buffer, &map);
            }
            gst_buffer_unref(buffer);
        }

        // 更新渲染
        SDL_RenderClear(priv->renderer);
        SDL_RenderCopy(priv->renderer, priv->texture, NULL, NULL);
        SDL_RenderPresent(priv->renderer);
    }

    return NULL;
}

// 数据处理函数
static GstFlowReturn media_videorender_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer) {
    MediaVideoRender *render = MEDIA_VIDEORENDER(parent);
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);

    // 音画同步
    GstClockTime timestamp = GST_BUFFER_PTS(buffer);
    GstClock *clock = gst_element_get_clock(GST_ELEMENT(render));
    if (clock) {
        GstClockTime current_time = gst_clock_get_time(clock);
        GstClockTime elapsed_time = current_time - priv->base_time;

        if (timestamp > elapsed_time) {
            GstClockID clock_id = gst_clock_new_single_shot_id(clock, timestamp);
            gst_clock_id_wait(clock_id, NULL);
            gst_clock_id_unref(clock_id);
        }
    }

    // 添加帧到队列
    pthread_mutex_lock(&priv->queue_mutex);
    if (g_queue_get_length(priv->frame_queue) >= 2) {
        GstBuffer *old_buffer = g_queue_pop_head(priv->frame_queue);
        gst_buffer_unref(old_buffer); // 丢弃旧帧
    }
    g_queue_push_tail(priv->frame_queue, gst_buffer_ref(buffer));
    pthread_cond_signal(&priv->queue_cond);
    pthread_mutex_unlock(&priv->queue_mutex);

    return GST_FLOW_OK;
}

// 创建texture的辅助函数
static SDL_Texture* create_texture_from_caps(MediaVideoRenderPrivate *priv, GstCaps *caps) {
    SDL_Texture *texture = NULL;

    if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)YV12"))) {
               g_print("Supported pixel format: YV12\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)IYUV"))) {
        g_print("Supported pixel format: IYUV\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)NV12"))) {
        g_print("Supported pixel format: NV12\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)NV21"))) {
        g_print("Supported pixel format: NV21\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_NV21, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)YUY2"))) {
        g_print("Supported pixel format: YUY2\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)UYVY"))) {
        g_print("Supported pixel format: UYVY\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_UYVY, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)RGB24"))) {
        g_print("Supported pixel format: RGB24\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)ARGB8888"))) {
        g_print("Supported pixel format: ARGB8888\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)RGBA8888"))) {
        g_print("Supported pixel format: RGBA8888\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)ABGR8888"))) {
        g_print("Supported pixel format: ABGR8888\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)BGRA8888"))) {
        g_print("Supported pixel format: BGRA8888\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)RGB565"))) {
        g_print("Supported pixel format: RGB565\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)BGR565"))) {
        g_print("Supported pixel format: BGR565\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else if (gst_caps_is_subset(caps, gst_caps_from_string("video/x-raw, format=(string)RGB332"))) {
        g_print("Supported pixel format: RGB332\n");
        texture = SDL_CreateTexture(priv->renderer, SDL_PIXELFORMAT_RGB332, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    } else {
        g_print("Unsupported pixel format\n");
    }

    return texture;
}

// sink event处理函数
static gboolean media_videorender_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    gboolean ret;
    MediaVideoRender *render = MEDIA_VIDEORENDER(parent);
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);
    g_print("media_videorender_sink_event: function is called for event: %s\n", GST_EVENT_TYPE_NAME(event));
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            gchar *caps_str = gst_caps_to_string(caps);
            g_print("media_videorender_sink_event: Received CAPS event with caps: %s\n", caps_str);
            g_free(caps_str);

            // 创建texture
            priv->texture = create_texture_from_caps(priv, caps);
            if (!priv->texture) {
                g_print("Could not create texture: %s\n", SDL_GetError());
                ret = FALSE;
                break;
            }
            break;
        }
        default:
            break;
    }

    // 将事件传递给下一个元素
    ret = gst_pad_event_default(pad, parent, event);
    return ret;
}

// 类初始化
static void media_videorender_class_init(MediaVideoRenderClass *klass) {
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_static_metadata(gstelement_class,
        "Video Render",
        "Render/Video",
        "Video rendering using SDL with A/V sync",
        "Your Name <youremail@example.com>");

    // 设置 sink pad 和 chain 函数
    GstPad *sink_pad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(sink_pad, GST_DEBUG_FUNCPTR(media_videorender_chain));
    gst_pad_set_event_function(sink_pad, GST_DEBUG_FUNCPTR(media_videorender_sink_event));
    gst_element_add_pad(GST_ELEMENT_CLASS(klass), sink_pad);
}

// 实例初始化
static void media_videorender_init(MediaVideoRender *render) {
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        g_print("SDL could not initialize: %s\n", SDL_GetError());
        return;
    }

    priv->window = SDL_CreateWindow("Video Render",
                                    SDL_WINDOWPOS_UNDEFINED,
                                    SDL_WINDOWPOS_UNDEFINED,
                                    800, 600, 0);
    if (!priv->window) {
        g_print("Could not create window: %s\n", SDL_GetError());
        SDL_Quit();
        return;
    }

    priv->renderer = SDL_CreateRenderer(priv->window, -1, SDL_RENDERER_ACCELERATED);
    if (!priv->renderer) {
        g_print("Could not create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(priv->window);
        SDL_Quit();
        return;
    }

    pthread_mutex_init(&priv->queue_mutex, NULL);
    pthread_cond_init(&priv->queue_cond, NULL);
    priv->frame_queue = g_queue_new();
    priv->stop_rendering = false;
    priv->base_time = gst_clock_get_time(gst_element_get_clock(GST_ELEMENT(render)));

    if (pthread_create(&priv->render_thread, NULL, render_loop, render) != 0) {
        g_print("Could not create render thread\n");
        SDL_DestroyRenderer(priv->renderer);
        SDL_DestroyWindow(priv->window);
        pthread_mutex_destroy(&priv->queue_mutex);
        pthread_cond_destroy(&priv->queue_cond);
        g_queue_free(priv->frame_queue);
        SDL_Quit();
        return;
    }
}

// 插件清理
static void media_videorender_finalize(GObject *object) {
    MediaVideoRender *render = MEDIA_VIDEORENDER(object);
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);

    priv->stop_rendering = true;
    pthread_cond_signal(&priv->queue_cond);
    pthread_join(priv->render_thread, NULL);

    pthread_mutex_destroy(&priv->queue_mutex);
    pthread_cond_destroy(&priv->queue_cond);
    g_queue_free_full(priv->frame_queue, (GDestroyNotify)gst_buffer_unref);

    SDL_DestroyTexture(priv->texture);
    SDL_DestroyRenderer(priv->renderer);
    SDL_DestroyWindow(priv->window);
    SDL_Quit();

    G_OBJECT_CLASS(media_videorender_parent_class)->finalize(object);
}

// 插件初始化
gboolean media_videorender_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "media_videorender", GST_RANK_NONE, TYPE_MEDIA_VIDEORENDER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    media_videorender,
    "Video Render Plugin",
    media_videorender_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://example.com/"
)