#include <SDL2/SDL.h>
#include <stdio.h>
#include <jni.h>
#include <android/log.h>

#define SCREEN_WIDTH  640 //横向像素个数
#define SCREEN_HEIGHT 480 //纵向像素个数


//将printf重定向到stderr
#define printf(...) fprintf(stderr,__VA_ARGS__)

//必须严格按照这种格式定义main函数，否则会出错
#ifdef __cplusplus
extern "C" {
#endif
int SDL_main(int argc, char* argv[])
{
    __android_log_print(ANDROID_LOG_VERBOSE, "MAIN", "SDL MAIN()");


    //初始化SDL（视频）并判断是否成功
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        //若失败，输出错误信息
        printf("SDL_Init error: %s\n", SDL_GetError());
        //直接退出整个程序
        return -1;
    }

    //创建窗口
    SDL_Window* window = SDL_CreateWindow(
            "SDL2",//窗口标题（支持UTF-8，但是必须将你的源文件也保存为UTF-8）
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,//SDL窗口默认位置（指定一个偏移量或居中）
            SCREEN_WIDTH, SCREEN_HEIGHT,//窗口大小
            SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL
    );
    //判断窗口是否创建成功
    if (!window)
    {
        printf("SDL_CreateWindow error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    SDL_Event windowEvent;
    while (true){
        if (SDL_PollEvent(&windowEvent)){
            if (SDL_QUIT == windowEvent.type){
                break;
            }
        }

        /* Select the color for drawing. It is set to red here. */
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);

        /* Clear the entire screen to our selected color. */
        SDL_RenderClear(renderer);

        /* Up until now everything was drawn behind the scenes.
           This will show the new, red contents of the window. */
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    //销毁窗口
    SDL_DestroyWindow(window);
    //退出SDL
    SDL_Quit();
    return 0;
}
#ifdef __cplusplus
}
#endif
