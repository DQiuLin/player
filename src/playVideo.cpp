#include "ffmpegutil.h"
#include <fstream>
#include <iostream>

extern "C"
{
#include "SDL2/SDL.h"
};

#define REFRESH_EVENT (SDL_USEREVENT + 1)

#define BREAK_EVENT (SDL_USEREVENT + 2)

#define VIDEO_FINISH (SDL_USEREVENT + 3)

using std::cout;
using std::endl;
using std::string;

namespace
{
using namespace ffmpegUtil;

int thread_exit = 0;

const int pixel_w = 1920;
const int pixel_h = 1080;
const int bpp = 12;
const int bufferSize = pixel_w * pixel_h * bpp / 8;
unsigned char buffer[bufferSize];

int refreshPicture(void *opaque)
{
    int timeInterval = *((int *)opaque);
    thread_exit = 0;
    while (!thread_exit)
    {
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(timeInterval); //延时
    }
    thread_exit = 0;
    // Break
    SDL_Event event;
    event.type = BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

void playMediaVideo(const string &inputFile)
{
    FrameGrabber grabber{inputFile, true, false};
    grabber.start();

    const int w = grabber.getWidth();
    const int h = grabber.getHeight();
    const auto fmt = AVPixelFormat(grabber.getPixelFormat());

    //初始化SDL系统
    if (SDL_Init(SDL_INIT_VIDEO))
    {
        //初始化失败
        string errMsg = "Could not init SDL";
        errMsg += SDL_GetError();
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    //创建窗口SDL_Window
    SDL_Window *screen;
    screen = SDL_CreateWindow("player", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, w, h,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!screen)
    {
        string errMsg = "SDL: could not create window - exiting:";
        errMsg += SDL_GetError();
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    //创建渲染器SDL_Renderer
    SDL_Renderer *sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

    //创建纹理SDL_Texture
    Uint32 pixFmt = SDL_PIXELFORMAT_IYUV;
    SDL_Texture *sdlTexture =
        SDL_CreateTexture(sdlRenderer, pixFmt, SDL_TEXTUREACCESS_STREAMING, w, h);

    std::ifstream is{inputFile, std::ios::binary};
    if (!is.is_open())
    {
        string errMsg = "cannot open this file:";
        errMsg += inputFile;
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    try
    {
        //用于延时
        int timeInterval = 1000 / (int)grabber.getFrameRate();
        cout << "timeInterval: " << timeInterval << endl;

        //创建一个线程
        SDL_Thread *refresh_thread =
            SDL_CreateThread(refreshPicture, "refreshPictureThread", &timeInterval);

        AVFrame *frame = av_frame_alloc();
        int ret;
        bool videoFinish = false;

        SDL_Event event; //代表一个事件

        struct SwsContext *sws_ctx = sws_getContext(
            w, h, fmt, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

        int numByte = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
        uint8_t *buffer = (uint8_t *)av_malloc(numByte * sizeof(uint8_t));
        AVFrame *pict = av_frame_alloc();
        av_image_fill_arrays(pict->data, pict->linesize, buffer, AV_PIX_FMT_YUV420P,
                             w, h, 32);

        while (true)
        {
            if (!videoFinish)
            {
                ret = grabber.grabImageFrame(frame);

                if (ret == 1)
                { // success.
                    sws_scale(sws_ctx, (uint8_t const *const *)frame->data,
                              frame->linesize, 0, h, pict->data, pict->linesize);
                }
                else if (ret == 0)
                { // no more frame.
                    cout << "VIDEO FINISHED." << endl;
                    videoFinish = true;
                    SDL_Event finishEvent;
                    finishEvent.type = VIDEO_FINISH;
                    SDL_PushEvent(&finishEvent);
                }
                else
                { // error.
                    string errMsg = "grabImageFrame error.";
                    cout << errMsg << endl;
                    throw std::runtime_error(errMsg);
                }
            }
            else
            {
                thread_exit = 1;
                break;
            }
            SDL_WaitEvent(&event); //等待一个事件
            if (event.type == REFRESH_EVENT)
            {
                SDL_UpdateYUVTexture(sdlTexture, NULL, pict->data[0], pict->linesize[0],
                                     pict->data[1], pict->linesize[1], pict->data[2],
                                     pict->linesize[2]); //设置纹理的数据

                SDL_RenderClear(sdlRenderer);                        //渲染器clear
                SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL); //将纹理的数据拷贝给渲染器
                SDL_RenderPresent(sdlRenderer);                      //显示
            }
            else if (event.type == SDL_QUIT) //退出
            {
                thread_exit = 1;
            }
            else if (event.type == BREAK_EVENT)
            {
                break;
            }
        }
        av_frame_free(&frame);
    }
    catch (std::exception ex)
    {
        cout << "Exception in play video" << ex.what() << endl;
    }
    catch (...)
    {
        cout << "Unknown exception in play media" << endl;
    }
    grabber.close();
}

void playVideo(const string &inputfile)
{
    cout << "play video: " << inputfile << endl;
    try
    {
        playMediaVideo(inputfile);
    }
    catch (std::exception ex)
    {
        cout << "exception: " << ex.what() << endl;
    }
}

} // namespace

int main()
{
    string inputFile = "/Users/dql/Desktop/01.mp4";
    playVideo(inputFile);
    return 0;
}