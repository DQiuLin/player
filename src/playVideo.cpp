#include "ffmpegUtil.h"
#include "mediaProcessor.hpp"

extern "C"
{
#include "SDL2/SDL.h"
};

using std::cout;
using std::endl;

// Refresh Event
#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define BREAK_EVENT (SDL_USEREVENT + 3)
#define VIDEO_FINISH (SDL_USEREVENT + 4)

void refreshPicture(int timeInterval, bool &exitRefresh, bool &faster)
{
    cout << "picRefresher timeInterval[" << timeInterval << "]" << endl;
    while (!exitRefresh)
    {
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        if (faster)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval / 2));
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
        }
    }
    cout << "[THREAD] picRefresher thread finished." << endl;
}

void playSdlVideo(VideoProcessor &vProcessor, AudioProcessor *audio = nullptr)
{
    //--------------------- GET SDL window READY -------------------

    auto width = vProcessor.getWidth();
    auto height = vProcessor.getHeight();

    //创建窗口SDL_Window
    SDL_Window *screen;
    // SDL 2.0 Support for multiple windows
    screen = SDL_CreateWindow(":-D Player", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, width, height,
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
        SDL_CreateTexture(sdlRenderer, pixFmt, SDL_TEXTUREACCESS_STREAMING, width, height);

    SDL_Event event;
    auto frameRate = vProcessor.getFrameRate();
    cout << "frame rate [" << frameRate << "]" << endl;

    bool exitRefresh = false;
    bool faster = false;
    std::thread refreshThread{refreshPicture, (int)(1000 / frameRate), std::ref(exitRefresh),
                              std::ref(faster)};

    int failCount = 0;
    int fastCount = 0;
    int slowCount = 0;
    while (!vProcessor.isStreamFinished())
    {
        SDL_WaitEvent(&event); //等待一个事件

        if (event.type == REFRESH_EVENT)
        {
            if (vProcessor.isStreamFinished())
            {
                exitRefresh = true;
                continue; // skip REFRESH event.
            }

            if (audio != nullptr)
            {
                auto vTs = vProcessor.getPts();
                auto aTs = audio->getPts();
                if (vTs > aTs && vTs - aTs > 30)
                {
                    cout << "VIDEO FASTER ================= vTs - aTs [" << (vTs - aTs)
                         << "]ms, SKIP A EVENT" << endl;
                    // skip a REFRESH_EVENT
                    faster = false;
                    slowCount++;
                    continue;
                }
                else if (vTs < aTs && aTs - vTs > 30)
                {
                    cout << "VIDEO SLOWER ================= aTs - vTs =[" << (aTs - vTs) << "]ms, Faster"
                         << endl;
                    faster = true;
                    fastCount++;
                }
                else
                {
                    faster = false;
                }
            }

            AVFrame *frame = vProcessor.getFrame();

            if (frame != nullptr)
            {
                SDL_UpdateYUVTexture(sdlTexture,
                                     NULL,
                                     //Y
                                     frame->data[0],
                                     frame->linesize[0],
                                     //U
                                     frame->data[1],
                                     frame->linesize[1],
                                     //V
                                     frame->data[2],
                                     frame->linesize[2]);            //设置纹理的数据
                SDL_RenderClear(sdlRenderer);                        //渲染器clear
                SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL); //将纹理的数据拷贝给渲染器
                SDL_RenderPresent(sdlRenderer);                      //显示

                if (!vProcessor.refreshFrame())
                {
                    cout << "WARN: vProcessor.refreshFrame false" << endl;
                }
            }
            else
            {
                failCount++;
                cout << "WARN: getFrame fail. failCount = " << failCount << endl;
            }
        }
        else if (event.type == SDL_QUIT)
        {
            cout << "SDL screen got a SDL_QUIT." << endl;
            exitRefresh = true;
            // close window.
            break;
        }
        else if (event.type == BREAK_EVENT)
        {
            break;
        }
    }

    refreshThread.join();
    cout << "[THREAD] Sdl video thread finish: failCount = " << failCount << ", fastCount = " << fastCount
         << ", slowCount = " << slowCount << endl;
}