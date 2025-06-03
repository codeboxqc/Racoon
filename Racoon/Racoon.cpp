//NuGet\Install-Package sdl2.nuget -Version 2.32.4
//NuGet\Install-Package sdl2_image.nuget -Version 2.8.8
//NuGet\Install-Package sdl2_ttf.nuget -Version 2.24.0
//NuGet\Install-Package sdl2_mixer.nuget -Version 2.8.1


#define SDL_MAIN_HANDLED
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_video.h>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <algorithm>
#include <chrono> // For timing

#include "Header.h" // Should include ImageData, loadImage, freeImageData, etc.
                   // And now FFmpeg related VideoContext and functions

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
TTF_Font* font = nullptr;

// Application state
enum AppState {
    STATE_FILE_BROWSER,
    STATE_IMAGE_VIEWER,
    STATE_TEXT_VIEWER,
    STATE_HEX_VIEWER,
    STATE_SOUND_PLAYER,
    STATE_VIDEO_PLAYER
};
AppState currentState = STATE_FILE_BROWSER;

// Current image data and texture
ImageData currentImage;
SDL_Texture* imageTexture = nullptr;
float currentZoom = 1.0f;
const float zoomSpeed = 0.1f;

int fileCount = 0;
int Sel = 0;
int Tag = 0;
#define MAX_DISPLAY 26
//#define MAX_FILES 1000 // Assumed, define in header.h if not present

FileInfo files[MAX_FILES];
wchar_t currentDir[MAX_PATH];
float rotorAngle = 0.0;

// Drive variables
static wchar_t** drives = nullptr;
static int driveCount = 0;

// Text viewer globals
std::vector<std::wstring> g_textFileContent;
int g_textScrollOffset = 0;
int g_textMaxScrollOffset = 0;
const int G_TEXT_VIEWER_LINES_PER_SCREEN = 28;

// Hex viewer globals
std::vector<unsigned char> g_hexFileBuffer;
int g_hexScrollOffset = 0;
int g_hexMaxScrollOffset = 0;
const int G_HEX_BYTES_PER_LINE = 16;
const int G_HEX_LINES_PER_SCREEN = 25;

// Sound player globals
Mix_Music* g_currentMusic = nullptr;
std::wstring g_currentPlayingSoundPath;

// Video player globals
VideoContext g_videoContext; // Manages FFmpeg state for the current video
SDL_Texture* g_videoTexture = nullptr; // Texture to render video frames
// void* g_videoStream = nullptr; // This will be replaced by g_videoContext
std::wstring g_currentPlayingVideoPath;

static bool showDrives = false;

// Double-click detection
static Uint32 lastClickTime = 0;
static int lastClickX = -1;
static int lastClickY = -1;
static const Uint32 DOUBLE_CLICK_TIME = 500;
static const int DOUBLE_CLICK_DISTANCE = 4;

char lettre[26][3];
int DI = 0;
int dela = 0;

// Window dimensions
constexpr int X = 800;
constexpr int Y = 600;

// Convert wstring to string for logging
std::string wstr_to_str(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}



// Initialize SDL2, SDL_ttf, SDL_mixer, and create a borderless window with a renderer
bool initSDL(const std::string & title, int width, int height, SDL_Window * &outWindow, SDL_Renderer * &outRenderer, TTF_Font * &outFont) {
    outWindow = nullptr;
    outRenderer = nullptr;
    outFont = nullptr;

    // Initialize SDL with video and audio subsystems
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        logError("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (TTF_Init() != 0) {
        logError("TTF_Init failed: %s", TTF_GetError());
        SDL_Quit();
        return false;
    }

    int mixer_flags = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC | MIX_INIT_MOD;
    int initialized_mixers = Mix_Init(mixer_flags);
    if ((initialized_mixers & mixer_flags) != mixer_flags) {
        logError("Mix_Init failed to initialize all loaders: %s", Mix_GetError());
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        logError("Mix_OpenAudio failed: %s", Mix_GetError());
        while (Mix_Init(0)) Mix_Quit();
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Log available video drivers using the new logError
    int numDrivers = SDL_GetNumVideoDrivers();
    logError("Available video drivers: %d", numDrivers);
    for (int i = 0; i < numDrivers; ++i) {
        logError("Driver %d: %s", i, SDL_GetVideoDriver(i));
    }

    SDL_Window* window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_SHOWN
    );
    if (!window) {
        logError("Borderless window creation failed: %s", SDL_GetError());
        window = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            width,
            height,
            SDL_WINDOW_SHOWN
        );
        if (!window) {
            logError("Standard window creation failed: %s", SDL_GetError());
            Mix_CloseAudio();
            while (Mix_Init(0)) Mix_Quit();
            TTF_Quit();
            SDL_Quit();
            return false;
        }
        logError("Fallback to standard window succeeded.");
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        logError("Renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        Mix_CloseAudio();
        while (Mix_Init(0)) Mix_Quit();
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) != 0) {
        logError("SDL_SetRenderDrawBlendMode failed: %s", SDL_GetError());
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_RenderSetLogicalSize(renderer, width, height) != 0) {
        logError("RenderSetLogicalSize failed: %s", SDL_GetError());
    }

    TTF_Font* font = TTF_OpenFont("arial.ttf", 20);
    if (!font) {
        logError("TTF_OpenFont failed for arial.ttf: %s", TTF_GetError());
        font = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 16);
        if (!font) {
            logError("TTF_OpenFont failed for system arial.ttf: %s", TTF_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            Mix_CloseAudio();
            while (Mix_Init(0)) Mix_Quit();
            TTF_Quit();
            SDL_Quit();
            return false;
        }
        logError("Fallback to system arial.ttf succeeded.");
    }

    if (SDL_SetRenderDrawColor(renderer, 33, 33, 33, 100) != 0) {
        logError("SetRenderDrawColor failed: %s", SDL_GetError());
    }
    if (SDL_RenderClear(renderer) != 0) {
        logError("RenderClear failed: %s", SDL_GetError());
    }
    SDL_RenderPresent(renderer);

    outWindow = window;
    outRenderer = renderer;
    outFont = font;
    return true;
}

// Clean up SDL resources
void cleanupSDL(SDL_Window * window, SDL_Renderer * renderer, TTF_Font * font) {
    logError("Starting application cleanup..."); // Simple string
    if (g_currentMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
    }

    logError("Cleaning up video player resources (FFmpeg)..."); // Simple string
    closeVideoFile(g_videoContext); // Release FFmpeg resources
    // The g_videoTexture is likely already handled by ESC or closeVideoFile,
    // but an extra check here doesn't hurt.
    if (g_videoTexture) {
        SDL_DestroyTexture(g_videoTexture);
        g_videoTexture = nullptr;
    }
    // Remove old g_videoStream cleanup:
    // if (g_videoStream) { free(g_videoStream); g_videoStream = nullptr; ... }

    Mix_CloseAudio();
    while (Mix_Init(0)) Mix_Quit();
    if (font) TTF_CloseFont(font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    logError("Application cleanup complete."); // Simple string
}

// Render text using SDL_ttf
void renderText(SDL_Renderer * renderer, TTF_Font * font, const std::wstring & text, int x, int y) {
    if (text.empty()) return;
    SDL_Color color = { 255, 255, 255, 255 };
    SDL_Surface* surface = TTF_RenderUNICODE_Solid(font, (const Uint16*)text.c_str(), color);
    if (!surface) {
        logError("TTF_RenderUNICODE_Solid failed: %s", TTF_GetError());
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        logError("CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dst = { x, y, surface->w, surface->h };
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

void Text(int x, int y, const std::wstring & text, int r, int g, int b) {
    if (text.empty()) return;
    SDL_Color color = { (Uint8)r, (Uint8)g, (Uint8)b, 255 };
    SDL_Surface* surface = TTF_RenderUNICODE_Solid(font, (const Uint16*)text.c_str(), color);
    if (!surface) {
        logError("TTF_RenderUNICODE_Solid failed: %s", TTF_GetError());
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        logError("CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dst = { x, y, surface->w, surface->h };
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

void Text(const char* text, int x, int y, int r, int g, int b) {
    if (!text || !*text) return;
    SDL_Color color = { (Uint8)r, (Uint8)g, (Uint8)b, 255 };
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
    if (!surface) {
        logError("TTF_RenderText_Blended failed: %s", TTF_GetError());
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        logError("CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dest = { x, y, surface->w, surface->h };
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

// Set a single pixel
void pixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 alpha) {
    if (x < 0 || x >= X || y < 0 || y >= Y) return;
    if (SDL_SetRenderDrawColor(renderer, r, g, b, alpha) != 0) {
        logError("setpixel: SetRenderDrawColor failed: %s", SDL_GetError());
    }
    if (SDL_RenderDrawPoint(renderer, x, y) != 0) {
        logError("setpixel: RenderDrawPoint failed: %s", SDL_GetError());
    }
}

// Bresenham line
void line(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        pixel(x1, y1, r, g, b, a);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

// Filled rectangle
void Rectanglefull(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_Rect rect = { x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderFillRect(renderer, &rect);
}

// Outline rectangle
void Rectangle(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_Rect rect = { x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
    SDL_RenderDrawRect(renderer, &rect);
}

void Spin(int x, int y, Uint8 r, Uint8 g, Uint8 b, float angleDegrees) {
    float radians = angleDegrees * M_PI / 180.0f;
    int length = 10;
    float x1 = x + cosf(radians) * length;
    float y1 = y + sinf(radians) * length;
    float x2 = x - cosf(radians) * length;
    float y2 = y - sinf(radians) * length;

    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
}

int begin() {
    DWORD len = GetCurrentDirectoryW(MAX_PATH, currentDir);
    if (len == 0 || len >= MAX_PATH) {
        logError("Failed to get current directory. Error: %lu", GetLastError());
        return 1;
    }

    fileCount = getFilesInExecutableDirectory(files, currentDir); // This function should use the new logError internally
    if (fileCount < 0) {
        // getFilesInExecutableDirectory already logs the specific error
        logError("Failed to list files (begin).");
        return 1;
    }
    logError("Found %d files in %s", fileCount, wstr_to_str(currentDir).c_str());
    return 0;
}

int update() {
    fileCount = getFilesInExecutableDirectory(files, currentDir); // This function should use the new logError internally
    if (fileCount < 0) {
        // getFilesInExecutableDirectory already logs the specific error
        logError("Failed to list files (update).");
        return 1;
    }
    logError("Updated file list: Found %d files in %s", fileCount, wstr_to_str(currentDir).c_str());
    if (Sel > fileCount - 1) Sel = fileCount - 1;
    return 0;
}

bool isImageFile(const wchar_t* extension) {
    if (!extension || !*extension) return false;
    const wchar_t* imageExtensions[] = {
        L"jpg", L"jpeg", L"jfif", L"png", L"bmp", L"gif", L"tiff", L"tif",
        L"webp", L"avif", L"ico", L"cur", L"pnm", L"ppm", L"pgm", L"pbm",
        L"xpm", L"xcf", L"pcx", L"tga", L"lbm", L"iff", L"xv", L"psd",
        L"hdr", L"pic", L"heif", L"heic", L"dds", L"wdp", L"jxr"
    };
    const int numExtensions = sizeof(imageExtensions) / sizeof(imageExtensions[0]);
    wchar_t lowerExt[16] = L"";
    wcsncpy_s(lowerExt, 16, extension, _TRUNCATE);
    for (int i = 0; lowerExt[i]; i++) {
        lowerExt[i] = towlower(lowerExt[i]);
    }
    for (int i = 0; i < numExtensions; i++) {
        if (_wcsicmp(lowerExt, imageExtensions[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool isSoundFile(const wchar_t* extension) {
    if (!extension || !*extension) return false;
    const wchar_t* soundExtensions[] = {
        L"mp3", L"mp2", L"wav", L"wave", L"ogg", L"flac",
        L"mid", L"midi", L"rmi", L"opus", L"aiff", L"aif",
        L"mod", L"s3m", L"xm", L"it", L"voc", L"au"
    };
    const int numExtensions = sizeof(soundExtensions) / sizeof(soundExtensions[0]);
    wchar_t lowerExt[16] = L"";
    wcsncpy_s(lowerExt, 16, extension, _TRUNCATE);
    for (int i = 0; lowerExt[i]; i++) {
        lowerExt[i] = towlower(lowerExt[i]);
    }
    for (int i = 0; i < numExtensions; i++) {
        if (_wcsicmp(lowerExt, soundExtensions[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool isVideoFile(const wchar_t* extension) {
    if (!extension || !*extension) return false;
    const wchar_t* videoExtensions[] = {
        L"mpg", L"mp4", L"mkv",
        L"webm", L"avi", L"mov",
        L"flv", L"ogv", L"m4a",
        L"aac", L"opus", L"spx"




    };
    const int numExtensions = sizeof(videoExtensions) / sizeof(videoExtensions[0]);
    wchar_t lowerExt[16] = L"";
    wcsncpy_s(lowerExt, 16, extension, _TRUNCATE);
    for (int i = 0; lowerExt[i]; i++) {
        lowerExt[i] = towlower(lowerExt[i]);
    }
    for (int i = 0; i < numExtensions; i++) {
        if (_wcsicmp(lowerExt, videoExtensions[i]) == 0) {
            return true;
        }
    }
    return false;
}

static const wchar_t* hexExtensions[] = {
    L"bin", L"dat", L"rom", L"hex", L"dump",
    L"exe", L"dll", L"obj", L"o", L"so", L"a",
    L"lib", L"sys", L"com", L"elf", L"out",
    L"img", L"iso", L"nrg", L"raw", L"mdf", L"vhd",
    L"vmdk", L"toast", L"dmg", L"sfs", L"vdi", L"hdd",
    L"efi", L"c32", L"bzImage", L"zImage", L"uImage",
    L"pif", L"scr", L"class", L"dsk", L"mod", L"prg",
    L"tap", L"sna", L"tzx", L"d64", L"g64", L"nib",
    L"sd", L"fd", L"mbr", L"boot", L"core", L"map",
    L"pak", L"wad", L"xp3", L"arc", L"cdi", L"rgn",
    L"firm", L"cap", L"upd", L"patch", L"blob", L"ttf",L"otf"
};

// Ensure the size of the array is accessible
const size_t hexExtensionsCount = sizeof(hexExtensions) / sizeof(hexExtensions[0]);

bool isHexViewableFile(const wchar_t* extension) {
    if (!extension || !*extension) return false;
    wchar_t lowerExt[16] = L"";
    wcsncpy_s(lowerExt, 16, extension, _TRUNCATE);
    for (int i = 0; lowerExt[i]; i++) {
        lowerExt[i] = towlower(lowerExt[i]);
    }
    for (size_t i = 0; i < hexExtensionsCount; i++) {
        if (_wcsicmp(lowerExt, hexExtensions[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool isTextFile(const wchar_t* extension) {
    if (!extension || !*extension) return false;
    const wchar_t* textExtensions[] = {
        // Basic text formats
        L"txt", L"log", L"md", L"rtf", L"csv", L"tsv",

        // Config & script files
        L"ini", L"cfg", L"conf", L"yaml", L"yml", L"json", L"xml",
        L"toml", L"env", L"props", L"properties",

        // Web-related
        L"html", L"htm", L"xhtml", L"css", L"js", L"ts", L"jsx", L"tsx",

        // Programming languages
        L"c", L"h", L"cpp", L"cc", L"cxx", L"hpp",
        L"cs", L"java", L"kt", L"kts", L"swift", L"m", L"mm",
        L"rs", L"go", L"py", L"pyw", L"rb", L"pl", L"pm",
        L"php", L"asp", L"aspx", L"jsp", L"lua", L"groovy",
        L"sh", L"bash", L"zsh", L"fish", L"bat", L"cmd", L"ps1",

        // Build and data formats
        L"makefile", L"mak", L"cmake", L"gradle", L"ninja", L"gyp",
        L"sql", L"db", L"dbs", L"schema", L"xsd",
        L"tex", L"ltx", L"bib",

        // Miscellaneous
        L"readme", L"license", L"changelog", L"todo"
    };
    const int numExtensions = sizeof(textExtensions) / sizeof(textExtensions[0]);
    wchar_t lowerExt[32] = L"";
    wcsncpy_s(lowerExt, 32, extension, _TRUNCATE);
    for (int i = 0; lowerExt[i]; i++) {
        lowerExt[i] = towlower(lowerExt[i]);
    }
    for (int i = 0; i < numExtensions; i++) {
        if (_wcsicmp(lowerExt, textExtensions[i]) == 0) {
            return true;
        }
    }
    return false;
}


struct RGBColor {
    int r, g, b;
};

RGBColor getFileTextColor(bool isDrive, bool isFolder, bool isImage, bool isText, bool isSelected) {
    return isSelected ? RGBColor{ 66, 244, 66 } : RGBColor{ 111, 111, 111 };
}

std::vector<std::wstring> loadTextFileContent(const wchar_t* filePath) {
    std::vector<std::wstring> lines;
    std::wifstream file(filePath);
    if (!file.is_open()) {
        logError("Failed to open text file: %s", wstr_to_str(filePath).c_str());
        return lines;
    }
    std::wstring line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    if (file.bad()) {
        logError("Error reading text file: %s", wstr_to_str(filePath).c_str());
    }
    file.close();
    return lines;
}

bool loadHexFileContent(const wchar_t* filePath, std::vector<unsigned char>&buffer) {
    buffer.clear();
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        logError("Failed to open file for hex view: %s", wstr_to_str(filePath).c_str());
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size == 0) {
        logError("File is empty (hex view): %s", wstr_to_str(filePath).c_str());
        file.close();
        return true;
    }
    try {
        buffer.resize(static_cast<size_t>(size));
    }
    catch (const std::bad_alloc& e) {
        logError("Failed to allocate memory for hex buffer (%s): %s", wstr_to_str(filePath).c_str(), e.what());
        file.close();
        return false;
    }
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        logError("Failed to read file content for hex view: %s", wstr_to_str(filePath).c_str());
        buffer.clear();
        file.close();
        return false;
    }
    file.close();
    return true;
}

std::wstring formatHexLine(const std::vector<unsigned char>&dataBuffer, size_t lineBaseOffset, int bytesPerLine) {
    std::wstringstream ss;
    ss << std::setw(8) << std::setfill(L'0') << std::hex << std::uppercase << lineBaseOffset << L": ";
    for (int i = 0; i < bytesPerLine; ++i) {
        if (lineBaseOffset + i < dataBuffer.size()) {
            ss << std::setw(2) << std::setfill(L'0') << std::hex << std::uppercase
                << static_cast<int>(dataBuffer[lineBaseOffset + i]) << L" ";
        }
        else {
            ss << L"   ";
        }
        if (i == (bytesPerLine / 2) - 1 && bytesPerLine > 1) {
            ss << L" ";
        }
    }
    ss << L" ";
    for (int i = 0; i < bytesPerLine; ++i) {
        if (lineBaseOffset + i < dataBuffer.size()) {
            unsigned char byte = dataBuffer[lineBaseOffset + i];
            ss << (byte >= 32 && byte <= 126 ? static_cast<wchar_t>(byte) : L'.');
        }
    }
    return ss.str();
}

char* wcharPathToCharPath(const wchar_t* wcharPath) {
    if (!wcharPath) return nullptr;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wcharPath, -1, NULL, 0, NULL, NULL);
    if (size_needed == 0) {
        logError("wcharPathToCharPath failed WideCharToMultiByte (size check). Error: %lu", GetLastError());
        return nullptr;
    }
    char* charPath = new (std::nothrow) char[size_needed];
    if (!charPath) {
        logError("wcharPathToCharPath: Memory allocation failed for size %d.", size_needed);
        return nullptr;
    }
    int bytes_converted = WideCharToMultiByte(CP_UTF8, 0, wcharPath, -1, charPath, size_needed, NULL, NULL);
    if (bytes_converted == 0) {
        logError("wcharPathToCharPath conversion failed WideCharToMultiByte. Error: %lu", GetLastError());
        delete[] charPath;
        return nullptr;
    }
    return charPath;
}

bool loadAndPlaySound(const wchar_t* filePath) {
    if (g_currentMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
    }

    char* filePath_char = wcharPathToCharPath(filePath);
    if (!filePath_char) {
        logError("loadAndPlaySound: Failed to convert file path %s for Mix_LoadMUS.", wstr_to_str(filePath).c_str());
        return false;
    }

    g_currentMusic = Mix_LoadMUS(filePath_char);
    delete[] filePath_char;

    if (!g_currentMusic) {
        logError("Mix_LoadMUS failed for %s: %s", wstr_to_str(filePath).c_str(), Mix_GetError());
        return false;
    }

    if (Mix_PlayMusic(g_currentMusic, 0) == -1) {
        logError("Mix_PlayMusic failed for %s: %s", wstr_to_str(filePath).c_str(), Mix_GetError());
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
        return false;
    }

    logError("Successfully loaded and started playing sound: %s", wstr_to_str(filePath).c_str());
    return true;
}

bool loadAndPlayVideo(const wchar_t* filePath) {
    // Clean up existing video resources (g_videoTexture might be recreated by decodeVideoFrame)
    if (g_videoTexture) {
        SDL_DestroyTexture(g_videoTexture);
        g_videoTexture = nullptr;
    }
    // Close any previously opened video
    closeVideoFile(g_videoContext); // Ensure previous video is closed before opening a new one

    char* filePath_char = wcharPathToCharPath(filePath);
    if (!filePath_char) {
        logError("loadAndPlayVideo: Failed to convert file path %s to char*", wstr_to_str(filePath).c_str());
        return false;
    }

    logError("loadAndPlayVideo: Attempting to open with FFmpeg: %s", filePath_char);
    if (openVideoFile(filePath_char, g_videoContext)) { // openVideoFile uses char*
        delete[] filePath_char; // filePath_char no longer needed
        g_currentPlayingVideoPath = filePath; // Keep wchar_t path for display
        logError("Successfully opened video with FFmpeg: %s", wstr_to_str(g_currentPlayingVideoPath).c_str());
        // Texture will be created/updated by decodeVideoFrame
        return true; // Signal success to Action function
    }
    else {
        delete[] filePath_char; // filePath_char no longer needed
        logError("Failed to open video with FFmpeg: %s", wstr_to_str(filePath).c_str());
        closeVideoFile(g_videoContext); // Clean up any partial state
        return false; // Signal failure
    }
}

int finddrive() {
    int letterCount = 0;
    wchar_t** drives = nullptr;
    int count = scanDriveLetters(&drives);
    if (count > 0) {
        for (int i = 0; i < count && letterCount < 26; i++) {
            if (drives[i] && wcslen(drives[i]) >= 2) {
                lettre[letterCount][0] = (char)drives[i][0];
                lettre[letterCount][1] = ':';
                lettre[letterCount][2] = '\0';
                letterCount++;
            }
        }
        for (int i = 0; i < count; i++) {
            free(drives[i]);
        }
        free(drives);
    }
    return letterCount;
}

void list(int fileC, int tag) {
    int xlist = 333;
    Rectangle(3, 3, X - 3, Y - 3, 88, 88, 88, 255);
    Rectangle(3, 3, X - 3, Y - 3, 99, 99, 99, 255);
    Rectanglefull(80, 8, 80 + xlist, 34, 22, 22, 22, 255);
    Rectangle(80, 8, 80 + xlist, 34, 100, 100, 100, 255);
    Rectanglefull(80, 40, 80 + xlist, Y - 20, 22, 22, 22, 255);
    Rectangle(80, 40, 80 + xlist, Y - 20, 100, 100, 100, 255);

    int cdxy = 28;
    int didi = DI;
    int startX[2] = { 8, 44 };
    int textX[2] = { 12, 48 };
    int startY = 40;
    int yStep = 36;

    for (int row = 0; row < 9; row++) {
        int y = startY + row * yStep;
        for (int col = 0; col < 2; col++) {
            Rectanglefull(startX[col], y, startX[col] + cdxy, y + cdxy, 77, 44, 44, 255);
            if (didi > 0) {
                Text(lettre[DI - didi], textX[col], y + 4, 100, 100, 100);
                didi--;
            }
        }
    }

    for (int ix = 0; ix < 7; ix++) {
        Rectangle(8, 40 + (ix * 36), 8 + cdxy, 40 + (ix * 36) + cdxy, 100, 100, 100, 255);
        Rectangle(44, 40 + (ix * 36), 44 + cdxy, 40 + (ix * 36) + cdxy, 100, 100, 100, 255);
    }

    int y = 42;
    const int maxDisplay = 26;
    for (int i = tag; i < tag + maxDisplay && i < fileC; i++) {
        wchar_t timeStr[64];
        filetimeToString(files[i].lastModified, timeStr, sizeof(timeStr) / sizeof(wchar_t));
        wchar_t line[512] = L"";
        wcsncpy_s(line, 31, files[i].filename, 30);

        bool isDrive = wcschr(files[i].filename, L'<') != nullptr || wcschr(files[i].filename, L'>') != nullptr;
        bool isFolder = wcschr(files[i].filename, L'<') != nullptr || wcschr(files[i].filename, L'>') != nullptr;
        bool isImage = isImageFile(files[i].extension);
        bool isText = isTextFile(files[i].extension);

        if (i == Sel) {
            Rectanglefull(88, y + 1, 74 + xlist, y + 22, 42, 42, 42, 255);
        }

        RGBColor color = getFileTextColor(isDrive, isFolder, isImage, isText, i == Sel);
        Text(86, y, line, color.r, color.g, color.b);
        y += 20;
    }

    Spin(18, 18, 0, 255, 0, rotorAngle);
    rotorAngle += 5.0f;
    if (rotorAngle >= 360.0f) rotorAngle -= 360.0f;
}

enum class FileType {
    ParentDir,
    CurrentDir,
    Folder,
    Drive,
    Other
};

FileType getFileType(const wchar_t* name) {
    if (!name) return FileType::Other;
    if (wcscmp(name, L"..") == 0) return FileType::ParentDir;
    if (wcscmp(name, L".") == 0) return FileType::CurrentDir;
    size_t len = wcslen(name);
    if (len >= 3 && name[0] == L'<' && name[len - 1] == L'>') return FileType::Folder;
    if ((len == 4 && name[0] == L'[' && name[2] == L':' && name[3] == L']') ||
        (len == 3 && name[1] == L':' && name[2] == L'\\')) return FileType::Drive;
    return FileType::Other;
}

void Action(wchar_t* filename, wchar_t* name) {
    if (!filename || !name) {
        logError("Action: Null filename or name provided.");
        return;
    }

    logError("Action invoked. filename: %s, name: %s", wstr_to_str(filename).c_str(), wstr_to_str(name).c_str());

    FileType type = getFileType(name);
    switch (type) {
    case FileType::ParentDir: {
        size_t len = wcslen(filename);
        if (len > 1 && filename[len - 1] == L'\\') filename[len - 1] = L'\0';
        wchar_t* lastSlash = wcsrchr(filename, L'\\');
        if (!lastSlash || lastSlash == filename) { // If at root like "C:\" or just "C:"
            logError("Action: Cannot go up from root directory: %s", wstr_to_str(filename).c_str());
            // Optional: if path is "C:\", allow navigating to drive list or do nothing.
            // For now, just return.
            return;
        }
        wchar_t parentDir[512] = L"";
        size_t charsToCopy = lastSlash - filename;
        wcsncpy_s(parentDir, 512, filename, charsToCopy);
        wcscpy_s(currentDir, 512, parentDir);
        break;
    }
    case FileType::CurrentDir: {
        if (wcslen(currentDir) >= 3 && currentDir[1] == L':' && currentDir[2] == L'\\') {
            wchar_t rootDir[4] = { currentDir[0], currentDir[1], currentDir[2], L'\0' };
            wcscpy_s(currentDir, 512, rootDir);
        }
        else {
            logError("Action: Invalid currentDir format for '.': %s", wstr_to_str(currentDir).c_str());
            return;
        }
        break;
    }
    case FileType::Folder: {
        wchar_t cleanName[MAX_PATH] = L"";
        wcsncpy_s(cleanName, MAX_PATH, name + 1, wcslen(name) - 2);
        wchar_t newDir[512] = L"";
        swprintf_s(newDir, 512, L"%s\\%s", currentDir, cleanName);
        wcscpy_s(currentDir, 512, newDir);
        break;
    }
    case FileType::Other:
    default: {
        wchar_t full_path_to_file[MAX_PATH];
        if (wcslen(currentDir) + wcslen(name) + 2 < MAX_PATH) {
            swprintf_s(full_path_to_file, MAX_PATH, L"%s\\%s", currentDir, name);
            std::wstring full_path_wstr(full_path_to_file);
            std::wstring filename_wstr(name); // Keep for specific messages if needed

            if (isHexViewableFile(files[Sel].extension)) {
                logError("Action: Matched hex viewable file: %s", wstr_to_str(full_path_wstr).c_str());
                g_hexFileBuffer.clear();
                if (loadHexFileContent(full_path_to_file, g_hexFileBuffer)) {
                    currentState = STATE_HEX_VIEWER;
                    g_hexScrollOffset = 0;
                    if (g_hexFileBuffer.empty()) {
                        g_hexMaxScrollOffset = 0;
                    }
                    else {
                        int num_lines = (g_hexFileBuffer.size() + G_HEX_BYTES_PER_LINE - 1) / G_HEX_BYTES_PER_LINE;
                        g_hexMaxScrollOffset = num_lines - G_HEX_LINES_PER_SCREEN;
                        if (g_hexMaxScrollOffset < 0) g_hexMaxScrollOffset = 0;
                    }
                    logError("Action: Successfully loaded for hex view: %s. Size: %zu bytes", wstr_to_str(full_path_wstr).c_str(), g_hexFileBuffer.size());
                    return;
                }
                else {
                    logError("Action: Failed to load for hex view: %s", wstr_to_str(full_path_wstr).c_str());
                }
            }
            else if (isSoundFile(files[Sel].extension)) {
                logError("Action: Matched sound file: %s", wstr_to_str(full_path_wstr).c_str());
                if (loadAndPlaySound(full_path_to_file)) {
                    g_currentPlayingSoundPath = full_path_wstr;
                    currentState = STATE_SOUND_PLAYER;
                    logError("Action: Switched to Sound Player state for: %s", wstr_to_str(full_path_wstr).c_str());
                    return;
                }
                else {
                    logError("Action: Failed to load/play sound: %s", wstr_to_str(full_path_wstr).c_str());
                }
            }
            else if (isTextFile(files[Sel].extension)) {
                logError("Action: Matched text file: %s", wstr_to_str(full_path_wstr).c_str());
                g_textFileContent.clear();
                g_textFileContent = loadTextFileContent(full_path_to_file);
                if (!g_textFileContent.empty()) {
                    currentState = STATE_TEXT_VIEWER;
                    g_textScrollOffset = 0;
                    if (g_textFileContent.size() > static_cast<size_t>(G_TEXT_VIEWER_LINES_PER_SCREEN)) {
                        g_textMaxScrollOffset = static_cast<int>(g_textFileContent.size()) - G_TEXT_VIEWER_LINES_PER_SCREEN;
                    }
                    else {
                        g_textMaxScrollOffset = 0;
                    }
                    logError("Action: Successfully loaded text file: %s. Lines: %zu", wstr_to_str(full_path_wstr).c_str(), g_textFileContent.size());
                    return;
                }
                else {
                    logError("Action: Failed to load text file or file is empty: %s", wstr_to_str(full_path_wstr).c_str());
                }
            }
            else if (isImageFile(files[Sel].extension)) {
                logError("Action: Matched image file: %s", wstr_to_str(full_path_wstr).c_str());
                if (currentImage.pixels != nullptr) freeImageData(&currentImage);
                if (imageTexture != nullptr) {
                    SDL_DestroyTexture(imageTexture);
                    imageTexture = nullptr;
                }
                currentImage = loadImage(full_path_to_file); // loadImage should use the new logError
                if (currentImage.pixels != nullptr) {
                    currentState = STATE_IMAGE_VIEWER;
                    logError("Action: Successfully loaded image: %s", wstr_to_str(full_path_wstr).c_str());
                    return;
                }
                else {
                    logError("Action: Failed to load image: %s", wstr_to_str(full_path_wstr).c_str());
                }
            }
            else if (isVideoFile(files[Sel].extension)) {
                logError("Action: Matched video file: %s", wstr_to_str(full_path_wstr).c_str());
                if (loadAndPlayVideo(full_path_to_file)) {
                    g_currentPlayingVideoPath = full_path_wstr;
                    currentState = STATE_VIDEO_PLAYER;
                    logError("Action: Switched to Video Player state for: %s", wstr_to_str(full_path_wstr).c_str());
                    if (g_videoContext.audioDevice != 0) {
                        SDL_PauseAudioDevice(g_videoContext.audioDevice, 0);
                        logError("FFmpeg: Audio playback started via Action.");
                    }
                }
                else {
                    logError("Action: Failed to load/play video: %s", wstr_to_str(full_path_wstr).c_str());
                }
                return;
            }
            else {
                logError("Action: File is not a supported viewable/playable type: %s", wstr_to_str(full_path_wstr).c_str());
            }
        }
        else {
            logError("Action: Full path too long for file: %s\\%s", wstr_to_str(currentDir).c_str(), wstr_to_str(name).c_str());
        }
        return; // Should be unreachable if all cases handled, but good for safety.
    }
    } // End switch(type)

    // This part is reached if type was ParentDir, CurrentDir, or Folder
    if (!goto_folder(currentDir)) { // goto_folder should use new logError
        // Error already logged by goto_folder
        return;
    }
    logError("Action: Navigated to directory: %s", wstr_to_str(currentDir).c_str());
    Sel = 0; // Reset selection
    Tag = 0; // Reset tag
    update();
}

void checkdrv(int mx, int my) {
    int cdxy = 28;
    int startX[2] = { 8, 44 };
    int startY = 40;
    int yStep = 36;
    int didi = DI;

    for (int row = 0; row < 9; row++) {
        int y = startY + row * yStep;
        for (int col = 0; col < 2; col++) {
            if (didi <= 0) break;
            SDL_Rect rect = { startX[col], y, cdxy, cdxy };
            if (mx >= rect.x && mx < rect.x + rect.w && my >= rect.y && my < rect.y + rect.h) {
                int index = DI - didi;
                changeDrive(lettre[index]); // changeDrive should use new logError
                // logError("Changed to drive: %s", lettre[index]); // changeDrive logs this
                Tag = 0;
                Sel = 0;
                char path[8];
                snprintf(path, sizeof(path), "%s\\", lettre[index]);
                wchar_t wpath[8];
                MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 8);
                wcscpy_s(currentDir, 512, wpath);
                update();
                break;
            }
            didi--;
        }
    }
    DI = finddrive();
}







int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    currentImage.pixels = nullptr;
    imageTexture = nullptr;
    currentZoom = 1.0f;

    if (!initSDL("Borderless File Manager", X, Y, window, renderer, font)) {
        logError("WinMain: Initialization failed. Exiting."); // initSDL logs specific errors
        return 1;
    }

    bool isDragging = false;
    int dragStartX = 0;
    int dragStartY = 0;
    int windowStartX = 0;
    int windowStartY = 0;

    if (begin() != 0) { // begin logs its own errors
        cleanupSDL(window, renderer, font);
        return 1;
    }

    if (!initializeFFmpeg()) { // initializeFFmpeg logs its own errors
        logError("WinMain: Failed to initialize FFmpeg. Video playback will not work.");
        // Potentially exit or disable video functionality
    }

    DI = finddrive(); // finddrive should use new logError (or be checked)

    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (currentState == STATE_IMAGE_VIEWER) {
                        currentState = STATE_FILE_BROWSER;
                        if (currentImage.pixels != nullptr) {
                            freeImageData(&currentImage); // Fixed typo: ¤tImage -> currentImage
                        }
                        if (imageTexture != nullptr) {
                            SDL_DestroyTexture(imageTexture);
                            imageTexture = nullptr;
                        }
                        currentZoom = 1.0f;
                    }
                    else if (currentState == STATE_TEXT_VIEWER) {
                        currentState = STATE_FILE_BROWSER;
                        g_textFileContent.clear();
                        g_textScrollOffset = 0;
                        g_textMaxScrollOffset = 0;
                        logError("Exiting text viewer, returning to file browser."); // Simple string
                    }
                    else if (currentState == STATE_HEX_VIEWER) {
                        currentState = STATE_FILE_BROWSER;
                        g_hexFileBuffer.clear();
                        g_hexScrollOffset = 0;
                        g_hexMaxScrollOffset = 0;
                        logError("Exiting hex viewer, returning to file browser."); // Simple string
                    }
                    else if (currentState == STATE_SOUND_PLAYER) {
                        logError("Exiting sound player, returning to file browser."); // Simple string
                        Mix_HaltMusic();
                        if (g_currentMusic != nullptr) {
                            Mix_FreeMusic(g_currentMusic);
                            g_currentMusic = nullptr;
                        }
                        g_currentPlayingSoundPath.clear();
                        currentState = STATE_FILE_BROWSER;
                    }
                    else if (currentState == STATE_VIDEO_PLAYER) {
                        Uint32 currentFlags = SDL_GetWindowFlags(window);
                        if (currentFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                            SDL_SetWindowFullscreen(window, 0); // Exit fullscreen
                            logError("Video Player: Exited fullscreen mode via ESC. Remaining in video player.");
                            // Remain in STATE_VIDEO_PLAYER
                        }
                        else {
                            // Original logic: Exit video player if not in fullscreen
                            logError("Video Player: Exiting video player (was windowed), returning to file browser via ESC.");
                            if (g_videoContext.audioDevice != 0) {
                                SDL_PauseAudioDevice(g_videoContext.audioDevice, 1); // 1 means pause
                                logError("FFmpeg: Audio playback paused on ESC (WinMain - exiting player).");
                            }
                            closeVideoFile(g_videoContext); // Clean up FFmpeg resources
                            if (g_videoTexture) {
                                SDL_DestroyTexture(g_videoTexture);
                                g_videoTexture = nullptr;
                            }
                            g_currentPlayingVideoPath.clear();
                            currentState = STATE_FILE_BROWSER;
                        }
                    }
                    else {
                        running = false;
                    }
                    break;
                case SDLK_UP:
                case SDLK_KP_8:
                    if (Sel > 0) {
                        Sel--;
                        if (Sel < Tag) {
                            Tag = Sel;
                        }
                    }
                    break;
                case SDLK_DOWN:
                case SDLK_KP_2:
                    if (Sel < fileCount - 1) {
                        Sel++;
                        if (Sel >= Tag + MAX_DISPLAY) {
                            Tag = Sel - MAX_DISPLAY + 1;
                        }
                    }
                    break;
                case SDLK_PAGEUP:
                case SDLK_KP_9:
                    if (Tag > 0) {
                        Tag -= MAX_DISPLAY;
                        if (Tag < 0) Tag = 0;
                        Sel = Tag;
                    }
                    else {
                        Sel = 0;
                    }
                    break;
                case SDLK_PAGEDOWN:
                case SDLK_KP_3:
                    if (fileCount <= MAX_DISPLAY) {
                        Sel = fileCount - 1;
                    }
                    else {
                        Sel = 25;
                        Tag = 0;
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                case SDLK_KP_ENTER:
                    Action(currentDir, files[Sel].filename);
                    break;
                case SDLK_s:
                    if (currentState == STATE_IMAGE_VIEWER && currentImage.pixels != nullptr) {
                        wchar_t outputPath[MAX_PATH];
                        swprintf_s(outputPath, MAX_PATH, L"%s\\output.jpg", currentDir);
                        if (saveImage(&currentImage, outputPath, SAVE_FORMAT_JPG, 100)) {
                            SDL_Log("Image saved to %ls", outputPath);
                        }
                        else {
                            SDL_Log("Failed to save image to %ls", outputPath);
                        }
                    }
                    break;
                }
                break;
            case SDL_MOUSEWHEEL:
                if (currentState == STATE_IMAGE_VIEWER) {
                    if (event.wheel.y > 0) {
                        currentZoom += zoomSpeed;
                    }
                    else if (event.wheel.y < 0) {
                        currentZoom -= zoomSpeed;
                    }
                    if (currentZoom < 0.1f) {
                        currentZoom = 0.1f;
                    }
                }
                else if (currentState == STATE_TEXT_VIEWER) {
                    const int scroll_speed = 3;
                    if (event.wheel.y > 0) {
                        g_textScrollOffset -= scroll_speed;
                    }
                    else if (event.wheel.y < 0) {
                        g_textScrollOffset += scroll_speed;
                    }
                    if (g_textScrollOffset < 0) {
                        g_textScrollOffset = 0;
                    }
                    if (g_textScrollOffset > g_textMaxScrollOffset) {
                        g_textScrollOffset = g_textMaxScrollOffset;
                    }
                }
                else if (currentState == STATE_HEX_VIEWER) {
                    const int scroll_speed = 3;
                    if (event.wheel.y > 0) {
                        g_hexScrollOffset -= scroll_speed;
                    }
                    else if (event.wheel.y < 0) {
                        g_hexScrollOffset += scroll_speed;
                    }
                    if (g_hexScrollOffset < 0) {
                        g_hexScrollOffset = 0;
                    }
                    if (g_hexScrollOffset > g_hexMaxScrollOffset) {
                        g_hexScrollOffset = g_hexMaxScrollOffset;
                    }
                }
                else {
                    if (event.wheel.y > 0) {
                        if (Sel > 0) {
                            Sel--;
                            if (Sel < Tag) {
                                Tag = Sel;
                            }
                        }
                    }
                    else if (event.wheel.y < 0) {
                        if (Sel < fileCount - 1) {
                            Sel++;
                            if (Sel >= Tag + MAX_DISPLAY) {
                                Tag = Sel - MAX_DISPLAY + 1;
                            }
                        }
                    }
                    if (fileCount <= MAX_DISPLAY) {
                        Tag = 0;
                    }
                    else if (Tag > fileCount - MAX_DISPLAY) {
                        Tag = fileCount - MAX_DISPLAY;
                    }
                    if (Tag < 0) Tag = 0;
                    if (Sel < 0) Sel = 0;
                    if (Sel >= fileCount) Sel = fileCount - 1;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    Uint32 currentTime = SDL_GetTicks();
                    int mx = event.button.x;
                    int my = event.button.y;

                    bool isDoubleClick = false;
                    if (currentTime - lastClickTime <= DOUBLE_CLICK_TIME &&
                        abs(mx - lastClickX) <= DOUBLE_CLICK_DISTANCE &&
                        abs(my - lastClickY) <= DOUBLE_CLICK_DISTANCE) {
                        isDoubleClick = true;
                    }

                    // Update last click info BEFORE any state-specific double-click handling
                    lastClickTime = currentTime;
                    lastClickX = mx;
                    lastClickY = my;

                    if (isDoubleClick) {
                        if (currentState == STATE_IMAGE_VIEWER && currentImage.pixels != nullptr && imageTexture != nullptr) {
                            SDL_Rect imageDisplayRect;
                            float scaledWidth = currentImage.width * currentZoom;
                            float scaledHeight = currentImage.height * currentZoom;
                            imageDisplayRect.w = (int)scaledWidth;
                            imageDisplayRect.h = (int)scaledHeight;
                            imageDisplayRect.x = (X - imageDisplayRect.w) / 2;
                            imageDisplayRect.y = (Y - imageDisplayRect.h) / 2;
                            if (imageDisplayRect.w <= 0) imageDisplayRect.w = 1;
                            if (imageDisplayRect.h <= 0) imageDisplayRect.h = 1;

                            if (mx >= imageDisplayRect.x && mx < imageDisplayRect.x + imageDisplayRect.w &&
                                my >= imageDisplayRect.y && my < imageDisplayRect.y + imageDisplayRect.h) {

                                Uint32 currentFlags = SDL_GetWindowFlags(window);
                                if (currentFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                                    SDL_SetWindowFullscreen(window, 0);
                                    logError("Exited fullscreen mode for image viewer."); // Simple
                                }
                                else {
                                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                                    logError("Entered fullscreen mode for image viewer."); // Simple
                                }
                                lastClickTime = 0;
                            }
                        }
                        else if (currentState == STATE_VIDEO_PLAYER && g_videoContext.formatContext != nullptr && g_videoTexture != nullptr) {
                            SDL_Rect videoDisplayRect;
                            // Ensure videoCodecContext is valid before accessing width/height
                            if (g_videoContext.videoCodecContext && g_videoContext.videoCodecContext->width > 0 && g_videoContext.videoCodecContext->height > 0) {
                                float videoAspectRatio = (float)g_videoContext.videoCodecContext->width / (float)g_videoContext.videoCodecContext->height;
                                float windowAspectRatio = (float)X / (float)Y;

                                if (windowAspectRatio > videoAspectRatio) {
                                    videoDisplayRect.h = Y;
                                    videoDisplayRect.w = (int)(Y * videoAspectRatio);
                                    videoDisplayRect.y = 0;
                                    videoDisplayRect.x = (X - videoDisplayRect.w) / 2;
                                }
                                else {
                                    videoDisplayRect.w = X;
                                    videoDisplayRect.h = (int)(X / videoAspectRatio);
                                    videoDisplayRect.x = 0;
                                    videoDisplayRect.y = (Y - videoDisplayRect.h) / 2;
                                }

                                if (mx >= videoDisplayRect.x && mx < videoDisplayRect.x + videoDisplayRect.w &&
                                    my >= videoDisplayRect.y && my < videoDisplayRect.y + videoDisplayRect.h) {

                                    Uint32 currentFlags = SDL_GetWindowFlags(window);
                                    if (currentFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                                        SDL_SetWindowFullscreen(window, 0);
                                        logError("Exited fullscreen mode for video player."); // Simple
                                    }
                                    else {
                                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                                        logError("Entered fullscreen mode for video player."); // Simple
                                    }
                                    lastClickTime = 0;
                                }
                            }
                        }
                        else {
                            // Fallthrough for file browser double-click
                            if (currentState == STATE_FILE_BROWSER) { // Ensure this only runs for file browser
                                const int listX = 88;
                                const int listY = 40;
                                const int listW = 333; // xlist value from list()
                                const int lineH = 20;
                                // Check if click is within the list_area (approximate, using xlist as width)
                                if (mx >= listX && mx <= listX + listW && my >= listY) {
                                    int index = (my - listY) / lineH;
                                    if (index >= 0 && index < MAX_DISPLAY && Tag + index < fileCount) {
                                        Sel = Tag + index;
                                        Action(currentDir, files[Sel].filename);
                                        Tag = 0;
                                        Sel = 0;
                                    }
                                }
                            }
                        }
                    } // End of isDoubleClick block

                    // Original single-click logic (outside isDoubleClick block)
                    // Also handles cases where double-click was not on image/video content
                    if (!isDoubleClick ||
                        (isDoubleClick && currentState == STATE_FILE_BROWSER) || // Allow file browser double click to also trigger drag start/selection
                        (isDoubleClick && currentState == STATE_IMAGE_VIEWER && lastClickTime != 0) || // lastClickTime would be 0 if fullscreen toggled
                        (isDoubleClick && currentState == STATE_VIDEO_PLAYER && lastClickTime != 0)) {

                        if (currentState == STATE_FILE_BROWSER) {
                            isDragging = true;
                            SDL_GetWindowPosition(window, &windowStartX, &windowStartY);
                            POINT pt = { mx, my };
                            HWND hwnd = GetActiveWindow();
                            ClientToScreen(hwnd, &pt);
                            dragStartX = pt.x - windowStartX;
                            dragStartY = pt.y - windowStartY;

                            const int listX = 88;
                            const int listY = 40;
                            const int listW = 333; // xlist value
                            const int lineH = 20;

                            if (mx >= listX && mx <= listX + listW && my >= listY) {
                                int index = (my - listY) / lineH;
                                if (index >= 0 && index < MAX_DISPLAY && Tag + index < fileCount) {
                                    Sel = Tag + index;
                                    logError("Selected file (single click): %s", wstr_to_str(files[Sel].filename).c_str());
                                }
                            }
                            checkdrv(mx, my); // checkdrv should use new logError
                        }
                        else if (currentState == STATE_VIDEO_PLAYER && !isDoubleClick) {
                            // Example: Single click in video player could toggle play/pause
                            // For now, just log, as per instructions.
                            logError("Single click in video player (no action currently defined)."); // Simple
                        }
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    isDragging = false;
                }
                break;
            case SDL_MOUSEMOTION:
                if (isDragging) {
                    POINT cursorPos;
                    if (GetCursorPos(&cursorPos)) {
                        SDL_SetWindowPosition(window, cursorPos.x - dragStartX, cursorPos.y - dragStartY);
                    }
                }
                break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        if (currentState == STATE_IMAGE_VIEWER) {
            if (currentImage.pixels != nullptr) {
                if (imageTexture == nullptr) {
                    Uint32 sdlPixelFormat;
                    int pitch;
                    if (currentImage.channels == 3) {
                        sdlPixelFormat = SDL_PIXELFORMAT_RGB24;
                        pitch = currentImage.width * 3;
                    }
                    else if (currentImage.channels == 4) {
                        sdlPixelFormat = SDL_PIXELFORMAT_RGBA32;
                        pitch = currentImage.width * 4;
                    }
                    else {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WinMain: Unsupported image channels: %d", currentImage.channels);
                        currentState = STATE_FILE_BROWSER;
                    }

                    if (currentState == STATE_IMAGE_VIEWER) {
                        SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
                            currentImage.pixels,
                            currentImage.width,
                            currentImage.height,
                            currentImage.channels * 8,
                            pitch,
                            (currentImage.channels == 3) ? 0x000000FF : 0x000000FF,
                            (currentImage.channels == 3) ? 0x0000FF00 : 0x0000FF00,
                            (currentImage.channels == 3) ? 0x00FF0000 : 0x00FF0000,
                            (currentImage.channels == 3) ? 0 : 0xFF000000
                        );

                        if (surface) {
                            imageTexture = SDL_CreateTextureFromSurface(renderer, surface);
                            SDL_FreeSurface(surface);
                            if (!imageTexture) {
                                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WinMain: Failed to create texture: %s", SDL_GetError());
                                freeImageData(&currentImage); // Fixed typo
                                currentState = STATE_FILE_BROWSER;
                            }
                        }
                        else {
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WinMain: Failed to create surface: %s", SDL_GetError());
                            freeImageData(&currentImage); // Fixed typo
                            currentState = STATE_FILE_BROWSER;
                        }
                    }
                }

                if (imageTexture != nullptr) {
                    SDL_Rect destRect;
                    if (currentImage.width > 0 && currentImage.height > 0) {
                        float scaledWidth = currentImage.width * currentZoom;
                        float scaledHeight = currentImage.height * currentZoom;
                        destRect.w = (int)scaledWidth;
                        destRect.h = (int)scaledHeight;
                        destRect.x = (X - destRect.w) / 2;
                        destRect.y = (Y - destRect.h) / 2;
                        if (destRect.w <= 0) destRect.w = 1;
                        if (destRect.h <= 0) destRect.h = 1;
                    }
                    else {
                        destRect = { 0, 0, 0, 0 };
                    }

                    if (destRect.w > 0 && destRect.h > 0) {
                        SDL_RenderCopy(renderer, imageTexture, nullptr, &destRect);
                    }
                    Text("S: Save (as output.jpg)", 10, Y - 60, 200, 200, 200);
                    Text("Mouse Wheel: Zoom", 10, Y - 40, 200, 200, 200);
                    Text("Esc: Close Image", 10, Y - 20, 200, 200, 200);
                }
            }
            else {
                currentState = STATE_FILE_BROWSER;
            }
        }
        else if (currentState == STATE_TEXT_VIEWER) {
            SDL_SetRenderDrawColor(renderer, 20, 20, 40, 255);
            SDL_RenderClear(renderer);

            int display_y = 10;
            int font_line_height = 20;
            int start_line = g_textScrollOffset;
            int num_lines_to_display = std::min(G_TEXT_VIEWER_LINES_PER_SCREEN, static_cast<int>(g_textFileContent.size()) - start_line);
            if (num_lines_to_display < 0) num_lines_to_display = 0;

            for (int i = 0; i < num_lines_to_display; ++i) {
                int current_line_index = start_line + i;
                if (current_line_index >= 0 && static_cast<size_t>(current_line_index) < g_textFileContent.size()) {
                    renderText(renderer, font, g_textFileContent[current_line_index], 10, display_y);
                    display_y += font_line_height;
                }
            }

            Text("Mouse Wheel: Scroll Text", 10, Y - 40, 200, 200, 200);
            Text("Esc: Close Text Viewer", 10, Y - 20, 200, 200, 200);
        }
        else if (currentState == STATE_HEX_VIEWER) {
            SDL_SetRenderDrawColor(renderer, 25, 25, 50, 255);
            SDL_RenderClear(renderer);

            int display_y = 10;
            int font_line_height = 20;

            if (!g_hexFileBuffer.empty()) {
                for (int i = 0; i < G_HEX_LINES_PER_SCREEN; ++i) {
                    size_t current_line_index_in_file = static_cast<size_t>(g_hexScrollOffset) + i;
                    size_t current_byte_offset = current_line_index_in_file * G_HEX_BYTES_PER_LINE;
                    if (current_byte_offset < g_hexFileBuffer.size()) {
                        std::wstring line_content = formatHexLine(g_hexFileBuffer, current_byte_offset, G_HEX_BYTES_PER_LINE);
                        renderText(renderer, font, line_content, 10, display_y);
                        display_y += font_line_height;
                    }
                    else {
                        break;
                    }
                }
            }
            else {
                renderText(renderer, font, L"File is empty or could not be loaded.", 10, display_y);
            }

            Text("Mouse Wheel: Scroll Hex Data", 10, Y - 40, 200, 200, 200);
            Text("Esc: Close Hex Viewer", 10, Y - 20, 200, 200, 200);
        }
        else if (currentState == STATE_SOUND_PLAYER) {
            SDL_SetRenderDrawColor(renderer, 40, 20, 40, 255);
            SDL_RenderClear(renderer);

            int display_y = 50;
            std::wstring displayText;
            if (!g_currentPlayingSoundPath.empty()) {
                size_t lastSlash = g_currentPlayingSoundPath.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) {
                    displayText = L"Playing: " + g_currentPlayingSoundPath.substr(lastSlash + 1);
                }
                else {
                    displayText = L"Playing: " + g_currentPlayingSoundPath;
                }
            }
            else {
                displayText = L"Playing sound...";
            }

            int textWidth = displayText.length() * 8;
            int text_x = (X - textWidth) / 2;
            if (text_x < 10) text_x = 10;
            renderText(renderer, font, displayText, text_x, display_y);
            display_y += 30;

            rotorAngle += 3.0f;
            if (rotorAngle >= 360.0f) rotorAngle -= 360.0f;
            Spin(X / 2, Y / 2, 0, 255, 255, rotorAngle);

            Text("Esc: Stop and Close Player", 10, Y - 20, 200, 200, 200);
        }
        else if (currentState == STATE_VIDEO_PLAYER) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);

            Uint32 frameStartTime = SDL_GetTicks(); // Time before decoding and rendering

            // Attempt to decode and get the next video frame as a texture
            if (g_videoContext.formatContext && g_videoContext.videoStreamIndex != -1 && decodeVideoFrame(g_videoContext, &g_videoTexture, renderer)) {
                if (g_videoTexture) {
                    // Render the video texture, maintaining its aspect ratio.
                    // Calculations are based on the logical rendering dimensions (X, Y) defined
                    // by SDL_RenderSetLogicalSize(). SDL handles the actual scaling to the
                    // window/fullscreen size. This ensures consistent aspect ratio logic
                    // regardless of window size or fullscreen state.
                    SDL_Rect dstRect;

                    // Prevent division by zero if codec context somehow has zero height.
                    // Though, if video is playing, width and height should be > 0.
                    if (g_videoContext.videoCodecContext->height == 0) {
                        logError("Video player: videoCodecContext->height is 0, cannot calculate aspect ratio.");
                        // Fallback: Render in a default small square or don't render.
                        // For now, let's make it a 0-size rect to avoid further issues.
                        dstRect = { 0, 0, 0, 0 };
                    }
                    else {
                        float videoAspectRatio = (float)g_videoContext.videoCodecContext->width / (float)g_videoContext.videoCodecContext->height;
                        float logicalWindowAspectRatio = (float)X / (float)Y;

                        if (logicalWindowAspectRatio > videoAspectRatio) {
                            // Logical window is wider than the video (or video is "taller" relative to window).
                            // Fit video to the height of the logical window (Y), and scale width accordingly.
                            dstRect.h = Y;
                            dstRect.w = (int)(Y * videoAspectRatio);
                            dstRect.y = 0;
                            dstRect.x = (X - dstRect.w) / 2; // Center horizontally
                        }
                        else {
                            // Logical window is taller than the video (or video is "wider" relative to window), or aspect ratios are the same.
                            // Fit video to the width of the logical window (X), and scale height accordingly.
                            dstRect.w = X;
                            dstRect.h = (int)(X / videoAspectRatio);
                            dstRect.x = 0;
                            dstRect.y = (Y - dstRect.h) / 2; // Center vertically
                        }
                    }

                    if (dstRect.w > 0 && dstRect.h > 0) { // Only render if dimensions are valid
                        SDL_RenderCopy(renderer, g_videoTexture, nullptr, &dstRect);
                    }
                }
            }
            else {
                // End of video or error (if formatContext was valid, otherwise it's not playing)
                if (g_videoContext.formatContext) {
                    // Video was playing or attempting to play
                    logError("Video ended or failed to decode video frame. Returning to file browser."); // Simple
                    if (g_videoContext.audioDevice != 0) {
                        SDL_PauseAudioDevice(g_videoContext.audioDevice, 1); // Pause audio
                        logError("FFmpeg: Audio playback paused due to video end/error (WinMain).");
                    }
                    closeVideoFile(g_videoContext); // This will also close the audio device
                    if (g_videoTexture) { SDL_DestroyTexture(g_videoTexture); g_videoTexture = nullptr; }
                    g_currentPlayingVideoPath.clear();
                    currentState = STATE_FILE_BROWSER;
                }
                else if (!g_currentPlayingVideoPath.empty() && currentState == STATE_VIDEO_PLAYER) {
                    // This case might happen if loadAndPlayVideo failed initially but state was set to VIDEO_PLAYER
                    logError("Video player state active but no video loaded/formatContext. Returning to file browser."); // Simple
                    // Ensure audio device is handled if it somehow got opened without formatContext
                    if (g_videoContext.audioDevice != 0) {
                        SDL_PauseAudioDevice(g_videoContext.audioDevice, 1);
                        logError("FFmpeg: Audio playback paused due to inconsistent state (WinMain).");
                        // closeVideoFile will be called if we transition state, or at cleanup.
                    }
                    g_currentPlayingVideoPath.clear();
                    currentState = STATE_FILE_BROWSER;
                }
            }

            // Display video information/controls
            if (!g_currentPlayingVideoPath.empty()) {
                std::wstring videoName = g_currentPlayingVideoPath;
                size_t lastSlash = videoName.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) {
                    videoName = videoName.substr(lastSlash + 1);
                }
                renderText(renderer, font, (L"Playing: " + videoName).c_str(), 10, 10);
                renderText(renderer, font, L"ESC: Stop and Close Video", 10, Y - 20);
            }
            else if (currentState == STATE_VIDEO_PLAYER) {
                // If somehow in video player state without a path (e.g. initial failed load)
                renderText(renderer, font, L"No video loaded.", 10, 10);
                renderText(renderer, font, L"ESC: Return to Browser", 10, Y - 20);
            }
        }
        else {
            list(fileCount, Tag);
        }

        SDL_RenderPresent(renderer);
    }

    // Cleanup after the main loop exits
    if (currentImage.pixels != nullptr) {
        freeImageData(&currentImage); // Fixed typo
    }
    if (imageTexture != nullptr) {
        SDL_DestroyTexture(imageTexture);
        imageTexture = nullptr;
    }
    cleanupSDL(window, renderer, font);
    return 0;
}