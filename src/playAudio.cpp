#include "ffmpegutil.h"
#include <fstream>
#include <iostream>

extern "C"
{
#include "SDL2/SDL.h"
};

using std::cout;
using std::endl;
using std::string;

namespace audio
{
using namespace ffmpegUtil;

//填充音频缓冲区的回调函数
void sdl_audio_callback(void *userdata, Uint8 *stream, int len)
{
    AudioUtil *audioUtil = (AudioUtil *)userdata;
    FrameGrabber *grabber = audioUtil->grabber;
    ReSampler *reSampler = audioUtil->reSampler;

    static AVFrame *aFrame = av_frame_alloc(); //存储音频帧

    int ret = grabber->grabAudioFrame(aFrame);
    // cout << "number of audio samples (per channel) described by this frame: " << aFrame->nb_samples << endl;

    if (ret == 2)
    {
        static uint8_t *outbuffer = nullptr;
        static int outBufferSize = 0;

        if (outbuffer == nullptr)
        {
            outBufferSize = reSampler->allocDataBuf(&outbuffer, aFrame->nb_samples);
            cout << "------audio samples: " << aFrame->nb_samples << endl;
        }
        else
        {
            memset(outbuffer, 0, outBufferSize);
        }

        int outSamples;
        int outDataSize;

        std::tie(outSamples, outDataSize) = reSampler->reSample(outbuffer, outBufferSize, aFrame);

        if (outDataSize != len)
        {
            cout << "WARNING: outDataSize[" << outDataSize << "]!= len[" << len << "]" << endl;
        }
        std::memcpy(stream, outbuffer, outDataSize);
    }
}

void playMediaAudio(const string &inputPath)
{
    FrameGrabber grabber{inputPath, false, true};
    grabber.start();

    int64_t inlayOut = grabber.getChannelLayout();
    int insampleRate = grabber.getSampleRate();
    int inchannels = grabber.getChannels();
    AVSampleFormat inFormat = AVSampleFormat(grabber.getSampleFormat());

    AudioInfo inAudio(inlayOut, insampleRate, inchannels, inFormat);
    AudioInfo outAudio = ReSampler::getDefaultAudioInfo(insampleRate);

    outAudio.sampleRate = inAudio.sampleRate;

    ReSampler reSampler(inAudio, outAudio);

    AudioUtil audioUtil{&grabber, &reSampler};

    //尝试解决缓冲区下溢问题
    if (!(SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE")))
    {
        SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
    }

    //初始化SDL系统
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        //初始化失败
        string errMsg = "Could not init SDL";
        errMsg += SDL_GetError();
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    // audio specs containers
    SDL_AudioSpec wanted_spec; // desired output format
    SDL_AudioSpec spec;        // actual output format

    // set audio settings from codec info
    wanted_spec.freq = grabber.getSampleRate();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = grabber.getChannels();
    wanted_spec.samples = 1024;
    wanted_spec.silence = 0;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = &audioUtil;

    //打开音频设备
    SDL_AudioDeviceID audioDeviceId;
    audioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);

    // SDL_OpenAudioDevice returns a valid device ID that is > 0 on success or 0 on failure.
    if (audioDeviceId == 0)
    {
        string errMsg = "Failed to open audio device:";
        errMsg += SDL_GetError();
        cout << errMsg << endl;
        throw std::runtime_error(errMsg);
    }

    cout << "wanted_specs.freq:" << wanted_spec.freq << endl;
    std::printf("wanted_specs.format: Ox%X\n", wanted_spec.format);
    cout << "wanted_specs.channels:" << (int)wanted_spec.channels << endl;
    cout << "wanted_spec.silence" << (int)wanted_spec.silence << endl;
    cout << "wanted_specs.samples:" << (int)wanted_spec.samples << endl;

    cout << "------------------------------------------------" << endl;

    cout << "specs.freq:" << spec.freq << endl;
    std::printf("specs.format: Ox%X\n", spec.format);
    cout << "specs.channels:" << (int)spec.channels << endl;
    cout << "specs.silence:" << (int)spec.silence << endl;
    cout << "specs.samples:" << (int)spec.samples << endl;

    cout << "------------------------------------------------" << endl
         << endl;

    cout << "waiting audio play..." << endl;

    SDL_PauseAudioDevice(audioDeviceId, 0); //0->播放; 1->暂停;
    SDL_Delay(300000);
    SDL_CloseAudio();
    SDL_Quit();
}

void playAudio(const string &inputfile)
{
    cout << "play audio: " << inputfile << endl;
    try
    {
        playMediaAudio(inputfile);
    }
    catch (std::exception ex)
    {
        cout << "exception: " << ex.what() << endl;
    }
}
} // namespace audio