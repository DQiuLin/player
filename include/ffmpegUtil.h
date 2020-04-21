#pragma once

#ifdef _WIN32
// Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
};
#else
// Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
};
#endif
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <tuple>

namespace ffmpegUtil
{
using std::cout;
using std::endl;
using std::string;
using std::stringstream;

struct ffutils
{
    static void initCodec(AVFormatContext *formatContext, int streamIndex,
                          AVCodecContext **avCodecContext)
    {
        string codecType{};
        switch (formatContext->streams[streamIndex]->codec->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            codecType = "video_codec";
            break;
        case AVMEDIA_TYPE_AUDIO:
            codecType = "audio_codec";
            break;
        default:
            throw std::runtime_error("error_decodec, unknowtype");
        }

        //获取解码器信息
        AVCodec *codec = avcodec_find_decoder(
            formatContext->streams[streamIndex]->codecpar->codec_id);

        if (codec == nullptr)
        {
            string errMsg = "Could not find codec";
            errMsg += (*avCodecContext)->codec_id;
            cout << errMsg << endl;
            throw std::runtime_error(errMsg);
        }

        // Allocate an AVCodecContext and set its fields to default values.
        (*avCodecContext) = avcodec_alloc_context3(codec);
        auto codecCtx = *avCodecContext;

        // Fill the codec context based on the values from the supplied codec
        // parameters
        if (avcodec_parameters_to_context(
                codecCtx, formatContext->streams[streamIndex]->codecpar) != 0)
        {
            string errorMsg = "Could not copy codec context";
            errorMsg += codec->name;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        if (avcodec_open2(codecCtx, codec, nullptr) < 0)
        {
            string errorMsg = "Could not open codec: ";
            errorMsg += codec->name;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        cout << codecType << "[" << codecCtx->codec->name
             << "] codec context initialize success" << endl;
    }
};

class PacketGrabber
{
    const string inputUrl;
    AVFormatContext *formatCtx = nullptr;
    bool fileGotToEnd = false;

    int videoIndex = -1;
    int audioIndex = -1;

public:
    ~PacketGrabber()
    {
        if (formatCtx != nullptr)
        {
            avformat_free_context(formatCtx);
            formatCtx = nullptr;
        }
        cout << "~PacketGrabber called." << endl;
    }
    PacketGrabber(const string &uri) : inputUrl(uri)
    {
        formatCtx = avformat_alloc_context();

        if (avformat_open_input(&formatCtx, inputUrl.c_str(), NULL, NULL) != 0)
        {
            string errorMsg = "Can not open input file:";
            errorMsg += inputUrl;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        if (avformat_find_stream_info(formatCtx, NULL) < 0)
        {
            string errorMsg = "Can not find stream information in input file:";
            errorMsg += inputUrl;
            cout << errorMsg << endl;
            throw std::runtime_error(errorMsg);
        }

        for (int i = 0; i < formatCtx->nb_streams; i++)
        {
            if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoIndex == -1)
            {
                videoIndex = i;
                cout << "video stream index = : [" << i << "]" << endl;
            }

            if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioIndex == -1)
            {
                audioIndex = i;
                cout << "audio stream index = : [" << i << "]" << endl;
            }
        }
    }

    /*
   *  return
   *          x > 0  : stream_index
   *          -1     : no more pkt
   */
    int grabPacket(AVPacket *pkt)
    {
        if (fileGotToEnd)
        {
            return -1;
        }
        while (true)
        {
            if (av_read_frame(formatCtx, pkt) >= 0)
            {
                return pkt->stream_index;
            }
            else
            {
                // file end;
                fileGotToEnd = true;
                return -1;
            }
        }
    }

    AVFormatContext *getFormatCtx() const { return formatCtx; }

    bool isFileEnd() const { return fileGotToEnd; }

    int getAudioIndex() const { return audioIndex; }
    int getVideoIndex() const { return videoIndex; }
};

struct AudioInfo
{
    int64_t layout;        //音频通道布局
    int sampleRate;        //采样率
    int channels;          //通道数
    AVSampleFormat format; //样本精度

    AudioInfo()
    {
        layout = -1;
        sampleRate = -1;
        channels = -1;
        format = AV_SAMPLE_FMT_S16;
    }

    AudioInfo(int64_t l, int sRate, int ch, AVSampleFormat fmt)
        : layout(l), sampleRate(sRate), channels(ch), format(fmt) {}
};

//重采样，改变音频的采样率等参数，使得音频按照我们期望的参数输出
class ReSampler
{
    SwrContext *swr; // 重采样结构体，改变音频的采样率等参数

public:
    ReSampler(const ReSampler &) = delete;
    ReSampler(ReSampler &&) noexcept = delete;
    ReSampler operator=(const ReSampler &) = delete;
    ~ReSampler()
    {
        cout << "~ReSampler called" << endl;
        if (swr != nullptr)
        {
            swr_free(&swr);
        }
    }

    const AudioInfo in;
    const AudioInfo out;

    static AudioInfo getDefaultAudioInfo(int sr)
    {
        int64_t layout = AV_CH_LAYOUT_STEREO;
        int sampleRate = sr;
        int channels = 2;
        AVSampleFormat format = AV_SAMPLE_FMT_S16;

        return ffmpegUtil::AudioInfo(layout, sampleRate, channels, format);
    }

    ReSampler(AudioInfo input, AudioInfo output) : in(input), out(output)
    {
        //分配SwrContext
        swr = swr_alloc_set_opts(nullptr, out.layout, out.format, out.sampleRate,
                                 in.layout, in.format, in.sampleRate, 0, nullptr);
        //设置完成后需要初始化
        if (swr_init(swr))
        {
            throw std::runtime_error("swr_init error");
        }
    }

    int allocDataBuf(uint8_t **outData, int inputSample)
    { //申请数据存储空间
        int bytePerOutSample = -1;
        switch (out.format)
        {
        case AV_SAMPLE_FMT_U8:
            bytePerOutSample = 1;
            break;
        case AV_SAMPLE_FMT_S16P:
        case AV_SAMPLE_FMT_S16:
            bytePerOutSample = 2;
            break;
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S32P:
        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_FLTP:
            bytePerOutSample = 4;
            break;
        case AV_SAMPLE_FMT_DBL:
        case AV_SAMPLE_FMT_DBLP:
        case AV_SAMPLE_FMT_S64:
        case AV_SAMPLE_FMT_S64P:
            bytePerOutSample = 8;
            break;
        default:
            bytePerOutSample = 2;
            break;
        }

        int outSamplesPerChannel = av_rescale_rnd(inputSample, out.sampleRate, in.sampleRate, AV_ROUND_UP);

        int outSize = outSamplesPerChannel * out.channels * bytePerOutSample;

        std::cout << "GuessOutSamplesPerChannel: " << outSamplesPerChannel << std::endl;
        std::cout << "GuessOutSize: " << outSize << std::endl;

        // Allocate a memory block with alignment suitable for all memory accesses.
        outSize *= 1.2;
        *outData = (uint8_t *)av_malloc(sizeof(uint8_t) * outSize);

        return outSize;
    }

    std::tuple<int, int> reSample(uint8_t *dataBuffer, int dataBufferSize, const AVFrame *aframe)
    {

        //Convert audio.
        int outSample = swr_convert(swr, &dataBuffer, dataBufferSize, (const uint8_t **)&aframe->data[0], aframe->nb_samples);

        if (outSample <= 0)
        {
            throw std::runtime_error("error: outSample=");
        }

        // Get the required buffer size for the given audio parameters.
        int outDataSize = av_samples_get_buffer_size(NULL, out.channels, outSample, out.format, 1);

        if (outDataSize <= 0)
        {
            throw std::runtime_error("error: outDataSize=");
        }
        return {outSample, outDataSize};
    }
};

} // namespace ffmpegUtil