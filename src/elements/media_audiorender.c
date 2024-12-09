#define PACKAGE "my_plugin"
#include "media_audiorender.h"
#include <gst/audio/gstaudiosink.h>
#include <SDL2/SDL.h>

struct _MediaAudioRender {
    GstAudioSink parent;
};

// 定义私有结构体
typedef struct _MediaAudioRenderPrivate {
    SDL_AudioDeviceID device_id;    // SDL 音频设备ID
    SDL_AudioSpec desired_spec;     // 期望的音频规格
} MediaAudioRenderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(MediaAudioRender, media_audiorender, GST_TYPE_AUDIO_SINK)

// 音频数据处理函数
static gboolean media_audiorender_prepare(GstAudioSink *sink, GstAudioRingBufferSpec *spec) {
    MediaAudioRender *render = MEDIA_AUDIORENDER(sink);
    MediaAudioRenderPrivate *priv = media_audiorender_get_instance_private(render);

    // 初始化 SDL 音频设备
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        g_error("SDL could not initialize! SDL_Error: %s", SDL_GetError());
        return FALSE;
    }

    SDL_zero(priv->desired_spec);
    priv->desired_spec.freq = spec->info.rate;
    priv->desired_spec.format = AUDIO_F32LSB;
    priv->desired_spec.channels = spec->info.channels;
    priv->desired_spec.samples = spec->segsize / (spec->info.channels * sizeof(float));
    priv->desired_spec.callback = NULL; // 不使用回调
    priv->desired_spec.userdata = NULL;

    priv->device_id = SDL_OpenAudioDevice(NULL, 0, &priv->desired_spec, NULL, 0);
    if (priv->device_id == 0) {
        g_error("Failed to open audio device: %s", SDL_GetError());
        return FALSE;
    }

    SDL_PauseAudioDevice(priv->device_id, 0); // 开始播放
    return TRUE;
}

static gboolean media_audiorender_unprepare(GstAudioSink *sink) {
    MediaAudioRender *render = MEDIA_AUDIORENDER(sink);
    MediaAudioRenderPrivate *priv = media_audiorender_get_instance_private(render);

    if (priv->device_id != 0) {
        SDL_CloseAudioDevice(priv->device_id);
        priv->device_id = 0;
    }

    SDL_Quit();
    return TRUE;
}

static gint media_audiorender_write(GstAudioSink *sink, gpointer data, guint length) {
    MediaAudioRender *render = MEDIA_AUDIORENDER(sink);
    MediaAudioRenderPrivate *priv = media_audiorender_get_instance_private(render);

    if (SDL_QueueAudio(priv->device_id, data, length) < 0) {
        g_printerr("Failed to send audio data to SDL: %s\n", SDL_GetError());
        return -1; // 返回负值表示错误
    }

    return (gint)length; // 返回写入的字节数
}

// 类初始化函数
static void media_audiorender_class_init(MediaAudioRenderClass *klass) {
    GstAudioSinkClass *audio_sink_class = GST_AUDIO_SINK_CLASS(klass);

    audio_sink_class->prepare = media_audiorender_prepare;
    audio_sink_class->unprepare = media_audiorender_unprepare;
    audio_sink_class->write = media_audiorender_write;

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
        "My Audio Render",
        "Sink/Audio",
        "Using SDL for audio rendering",
        "Your Name <youremail@example.com>");
}

// 实例初始化函数
static void media_audiorender_init(MediaAudioRender *render) {
    // 初始化私有数据
    MediaAudioRenderPrivate *priv = media_audiorender_get_instance_private(render);
    priv->device_id = 0;
}

// 插件初始化函数
gboolean media_audiorender_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "media_audiorender", GST_RANK_NONE, TYPE_MEDIA_AUDIORENDER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    media_audiorender,
    "My Audio Render Plugin",
    media_audiorender_plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://example.com/"
)
