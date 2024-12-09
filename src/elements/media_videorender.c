#define PACKAGE "my_plugin"
#include "media_videorender.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdbool.h>
#include <glib.h>

typedef struct _MediaVideoRenderPrivate {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    GQueue *frame_queue;
    bool stop_rendering;
    GstClockTime base_time;
    pthread_t render_thread;
} MediaVideoRenderPrivate;

struct _MediaVideoRender {
    GstVideoSink parent;
};

G_DEFINE_TYPE_WITH_PRIVATE(MediaVideoRender, media_videorender, GST_TYPE_VIDEO_SINK)

// 渲染线程函数
static void* render_loop(void *data) {
    MediaVideoRender *render = MEDIA_VIDEORENDER(data);
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);

    while (!priv->stop_rendering) {
        pthread_mutex_lock(&priv->queue_mutex);

        while (g_queue_is_empty(priv->frame_queue) && !priv->stop_rendering) {
            pthread_cond_wait(&priv->queue_cond, &priv->queue_mutex);
        }

        if (priv->stop_rendering) {
            pthread_mutex_unlock(&priv->queue_mutex);
            break;
        }

        GstBuffer *buffer = g_queue_pop_head(priv->frame_queue);
        pthread_mutex_unlock(&priv->queue_mutex);

        if (buffer) {
            GstMapInfo map;
            if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                SDL_UpdateTexture(priv->texture, NULL, map.data, 800 * 3 / 2);
                gst_buffer_unmap(buffer, &map);
            }
            gst_buffer_unref(buffer);
        }

        SDL_RenderClear(priv->renderer);
        SDL_RenderCopy(priv->renderer, priv->texture, NULL, NULL);
        SDL_RenderPresent(priv->renderer);
    }

    return NULL;
}

// 数据处理函数
static GstFlowReturn media_videorender_show_frame(GstVideoSink *sink, GstBuffer *buffer) {
    MediaVideoRender *render = MEDIA_VIDEORENDER(sink);
    MediaVideoRenderPrivate *priv = media_videorender_get_instance_private(render);

    pthread_mutex_lock(&priv->queue_mutex);
    if (g_queue_get_length(priv->frame_queue) >= 2) {
        GstBuffer *old_buffer = g_queue_pop_head(priv->frame_queue);
        gst_buffer_unref(old_buffer);
    }
    g_queue_push_tail(priv->frame_queue, gst_buffer_ref(buffer));
    pthread_cond_signal(&priv->queue_cond);
    pthread_mutex_unlock(&priv->queue_mutex);

    return GST_FLOW_OK;
}

// 类初始化
static void media_videorender_class_init(MediaVideoRenderClass *klass) {
    GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS(klass);

    video_sink_class->show_frame = media_videorender_show_frame;

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
        "Video Render",
        "Sink/Video",
        "Video rendering using SDL with A/V sync",
        "Your Name <youremail@example.com>");
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