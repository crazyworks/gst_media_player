#include <gst/gst.h>
#include "audio_player.h"
#include "video_player.h"
#include "media_player.h"
#include "audio_player2.h"

#define FILE_PATH "/Users/lizhen/Downloads/share_5580ecbe4ade3443b8ae9a0649c93b3e1728631581744.mp4"

int my_main(int argc, char *argv[], gpointer user_data) {
    // 实现你的主函数逻辑
    video_player(FILE_PATH);
    return 0;
}

// Call gst_macos_main in the main function
int main(int argc, char *argv[]) {
    // Use gst_macos_main to run the my_main function
    return gst_macos_main(my_main, argc, argv, NULL);
}
