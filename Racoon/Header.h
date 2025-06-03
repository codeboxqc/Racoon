#pragma once
#include <windows.h>
#include <string>
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

#define MAX_FILES 5000

struct ImageData {
    unsigned char* pixels;
    unsigned int width;
    unsigned int height;
    int channels;
};

// Ensure FileInfo is defined only once

typedef struct {
    wchar_t filename[MAX_PATH];
    wchar_t extension[16];
    LARGE_INTEGER size;
    FILETIME lastModified;
    DWORD attributes;
} FileInfo;




// Function declarations
int getFilesInExecutableDirectory(FileInfo files[], const wchar_t* directory);
int ren(const wchar_t* infile, const wchar_t* newname);
int del(const wchar_t* filename);
std::wstring attributesToString(DWORD attr);
void filetimeToString(FILETIME ft, wchar_t* buffer, size_t len);
std::string cc(const wchar_t* buf);
bool goto_folder(wchar_t* path);
void freeDriveLetters(wchar_t** drives, int driveCount);
int scanDriveLetters(wchar_t*** drives);
int changeDrive(const char* drive);

ImageData loadImage(const wchar_t* imagePath);
struct SDL_Renderer;
struct SDL_Rect;
void displayImage(SDL_Renderer* renderer, ImageData* imageData, SDL_Rect destinationRect);
void freeImageData(ImageData* imageData);
bool loadWithWIC(const wchar_t* imagePath, ImageData& imageData);
bool loadWithWebP(const wchar_t* imagePath, ImageData& imageData);
bool loadWithAVIF(const wchar_t* imagePath, ImageData& imageData);

void logError(const char* format, ...);

enum ImageSaveFormat {
    SAVE_FORMAT_PNG,
    SAVE_FORMAT_BMP,
    SAVE_FORMAT_TGA,
    SAVE_FORMAT_JPG,
    SAVE_FORMAT_WEBP,
    SAVE_FORMAT_AVIF
};

bool saveImage(ImageData* imageData, const wchar_t* outputPath, ImageSaveFormat format, int jpegQuality);

struct VideoContext {
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* videoCodecContext = nullptr;
    AVCodecContext* audioCodecContext = nullptr;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    struct SwsContext* swsContext = nullptr;
    AVFrame* decodedFrame = nullptr;
    AVFrame* rgbFrame = nullptr;
    uint8_t* rgbBuffer = nullptr;

    AVPacket* audioPacket = nullptr;
    AVFrame* decodedAudioFrame = nullptr;
    uint8_t* audioBuffer = nullptr;
    uint32_t audioBufferPos = 0;
    uint32_t audioBufferSize = 0;
    uint32_t audioBufferAllocatedSize = 0;
    SDL_AudioSpec desiredAudioSpec = { 0 };
    SDL_AudioSpec obtainedAudioSpec = { 0 };
    SDL_AudioDeviceID audioDevice = 0;
    struct SwrContext* swrContext = nullptr;


    AVChannelLayout audioChannelLayout = { AV_CHANNEL_ORDER_UNSPEC, 0, { 0 }, nullptr };
};

extern "C" void audioCallback(void* userdata, uint8_t* stream, int len);

bool initializeFFmpeg();
bool openVideoFile(const char* filePath, VideoContext& videoCtx);
void closeVideoFile(VideoContext& videoCtx);
bool decodeVideoFrame(VideoContext& videoCtx, SDL_Texture** videoTexture, SDL_Renderer* renderer);
bool decodeNextAudioPacket(VideoContext& videoCtx);
// Add the missing #if directive
#ifdef __cplusplus
#endif

// Standardized logging function

 