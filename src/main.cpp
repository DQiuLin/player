#include <iostream>
using std::cout;
using std::endl;
using std::string;

namespace video
{
extern void playVideo(const string &inputfile);
}

namespace audio
{
extern void playAudio(const string &inputfile);
}

int main()
{
    string inputFile = "/Users/dql/Desktop/01.mp4";
    // video::playVideo(inputFile); //单独播放音频时注释此句
    audio::playAudio(inputFile); //单独播放视频时注释此句
    return 0;
}