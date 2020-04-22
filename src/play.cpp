#include "ffmpegUtil.h"
#include "mediaProcessor.hpp"

#include <iostream>
#include <string>
#include <list>
#include <memory>
#include <chrono>
#include <thread>

extern "C"
{
#include "SDL2/SDL.h"
};

extern void startSdlAudio(SDL_AudioDeviceID &audioDeviceID, AudioProcessor &aProcessor);
extern void playSdlVideo(VideoProcessor &vProcessor, AudioProcessor *audio = nullptr);

namespace
{

using namespace ffmpegUtil;

using std::cout;
using std::endl;

void pktReader(PacketGrabber &pGrabber, AudioProcessor *aProcessor,
               VideoProcessor *vProcessor)
{
    const int CHECK_PERIOD = 10;

    cout << "INFO: pkt Reader thread started." << endl;
    int audioIndex = aProcessor->getAudioIndex();
    int videoIndex = vProcessor->getVideoIndex();

    while (!pGrabber.isFileEnd() && !aProcessor->isClosed() && !vProcessor->isClosed())
    {
        while (aProcessor->needPacket() || vProcessor->needPacket())
        {
            AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));
            int t = pGrabber.grabPacket(packet);
            if (t == -1)
            {
                cout << "INFO: file finish." << endl;
                aProcessor->pushPkt(nullptr);
                vProcessor->pushPkt(nullptr);
                break;
            }
            else if (t == audioIndex && aProcessor != nullptr)
            {
                unique_ptr<AVPacket> uPacket(packet);
                aProcessor->pushPkt(std::move(uPacket));
            }
            else if (t == videoIndex && vProcessor != nullptr)
            {
                unique_ptr<AVPacket> uPacket(packet);
                vProcessor->pushPkt(std::move(uPacket));
            }
            else
            {
                av_packet_free(&packet);
                cout << "WARNING: unknown streamIndex: [" << t << "]" << endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_PERIOD));
    }
    cout << "[THREAD] INFO: pkt Reader thread finished." << endl;
}

int play(const string &inputFile)
{
    // create packet grabber
    PacketGrabber packetGrabber{inputFile};
    auto formatCtx = packetGrabber.getFormatCtx();
    av_dump_format(formatCtx, 0, "", 0); //print

    // create VideoProcessor
    VideoProcessor videoProcessor(formatCtx);
    videoProcessor.start();

    // create AudioProcessor
    AudioProcessor audioProcessor(formatCtx);
    audioProcessor.start();

    // start pkt reader
    std::thread readerThread{pktReader, std::ref(packetGrabber), &audioProcessor,
                             &videoProcessor};

    //尝试解决缓冲区下溢问题
    if (!(SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE")))
    {
        SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
    }

    //初始化SDL系统
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        //初始化失败
        string errMsg = "Could not initialize SDL - ";
        errMsg += SDL_GetError();
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    SDL_AudioDeviceID audioDeviceID;

    std::thread startAudioThread(startSdlAudio, std::ref(audioDeviceID),
                                 std::ref(audioProcessor));

    playSdlVideo(videoProcessor, &audioProcessor);

    SDL_PauseAudioDevice(audioDeviceID, 1);
    SDL_CloseAudio();

    bool r;
    r = audioProcessor.close();
    cout << "audioProcessor closed: " << r << endl;
    r = videoProcessor.close();
    cout << "videoProcessor closed: " << r << endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    readerThread.join();
    startAudioThread.join();
    cout << "Pause and Close audio" << endl;

    return 0;
}

} // namespace

void playVideoWithAudio(const string &inputFile)
{
    std::cout << "playVideoWithAudio: " << inputFile << std::endl;
    play(inputFile);
}