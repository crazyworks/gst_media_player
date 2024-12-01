cmake_minimum_required(VERSION 3.10)
project(gst_media_player C)

set(CMAKE_C_STANDARD 99)

# set(GSTREAMER_PATH "/opt/homebrew/lib/gstreamer-1.0")
# 查找 FFmpeg 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libavfilter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

message(STATUS "GStreamer libraries found at: ${GSTREAMER_LIBRARY_DIRS}")
message(STATUS "GStreamer libraries: ${GSTREAMER_LIBRARIES}")

# 查找 GLib 和 GObject 库
pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0)

# 查找 SDL 库
pkg_check_modules(SDL2 REQUIRED sdl2)

# 包含头文件目录
include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    src/audioplayer
    src/elements
    src/mediaplayer
    src/videoplayer
)

# 源文件列表
set(SRC_FILES
    main.c
    src/audioplayer/audio_player.c
    src/audioplayer/audio_player2.c
    src/elements/audio_demux.c
    src/elements/audio_demux2.c
    src/elements/media_demux.c
    src/elements/media_parser.c
    src/elements/my_adec.c
    src/elements/my_audiorender.c
    src/elements/my_demux.c
    src/elements/media_h264parse.c
    src/elements/media_h265parse.c
    src/elements/media_vdec.c
    src/elements/media_videorender.c
    src/elements/media_demux.c
    src/mediaplayer/media_player.c
    src/videoplayer/video_player.c
)

# 创建可执行文件
add_executable(gst_media_player ${SRC_FILES})

# 链接库
target_link_libraries(gst_media_player
    ${GSTREAMER_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${SDL2_LIBRARIES}
    pthread
)

target_link_directories(gst_media_player PRIVATE
    ${GSTREAMER_LIBRARY_DIRS}
    ${FFMPEG_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${SDL2_LIBRARY_DIRS}
)

# 添加编译选项
target_compile_options(gst_media_player PRIVATE
    ${FFMPEG_CFLAGS_OTHER}
    ${GLIB_CFLAGS_OTHER}
)