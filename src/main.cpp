#include <iostream>
using std::string;

extern void playVideoWithAudio(const string &inputfile);

int main()
{
    string inputFile = "/Users/dql/Downloads/test.mp4";
    playVideoWithAudio(inputFile);
    return 0;
}