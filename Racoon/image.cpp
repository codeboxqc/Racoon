#define STB_IMAGE_IMPLEMENTATION
 #include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
 #include <stb_image_write.h>
//nclude "stb_image.h"
//nclude "stb_image_write.h"
#include <wincodec.h> // For WIC
#include <shlwapi.h>  // For PathFindExtensionW
#pragma comment(lib, "windowscodecs.lib") // Link against the WIC library

 
#include <webp/decode.h>
#include <webp/encode.h>
#include <avif/avif.h>
#include <stdio.h>

#include <SDL2/SDL_image.h>

#pragma comment(lib, "shlwapi.lib")

#include "Header.h"
#include <string>
#include <fstream> // For reading file into buffer for libwebp
#include <cstdio>  // For _wfopen_s with wchar_t, and general file operations
#include <locale>
#include <codecvt> // For std::codecvt_utf8_utf16
#include <vector>  // For std::vector

#include <SDL2/SDL.h>

 

// Refactored loadImage function
ImageData loadImage(const wchar_t* imagePath) {
    ImageData imageData = { nullptr, 0, 0, 0 };

    // 1. Path Conversion
    if (imagePath == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Null imagePath provided to loadImage.");
        return imageData;
    }

    size_t pathLen = wcslen(imagePath);
    if (pathLen == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Empty imagePath provided to loadImage.");
        return imageData;
    }

    std::vector<char> utf8PathVec;
    utf8PathVec.resize(pathLen * 4 + 1); // Max 4 bytes per UTF-8 char + null terminator

    size_t convertedChars = 0;
    errno_t err = wcstombs_s(&convertedChars, utf8PathVec.data(), utf8PathVec.size(), imagePath, _TRUNCATE);

    if (err != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error converting imagePath '%ls' to UTF-8. wcstombs_s error: %d", imagePath, err);
        return imageData;
    }

    // wcstombs_s includes the null terminator in convertedChars on success if space allows.
    // If pathLen > 0 and convertedChars is 0 (or 1 if only null terminator was written for an empty logical string), it's an issue.
    if (convertedChars == 0 && pathLen > 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "UTF-8 conversion of '%ls' resulted in zero characters (excluding null terminator).", imagePath);
        return imageData;
    }
    // Ensure null termination if _TRUNCATE happened and exactly filled the buffer without space for null.
    // However, wcstombs_s guarantees null termination on success if err is 0, even with _TRUNCATE,
    // provided the buffer size is > 0. Our buffer is pathLen*4+1, so it's always > 0 if pathLen > 0.
    // If convertedChars == utf8PathVec.size(), it means truncation occurred. The string is null-terminated.

    const char* utf8PathCStr = utf8PathVec.data();

    // 2. SDL_image Loading Attempt
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Attempting to load '%s' with SDL_image...", utf8PathCStr);
    SDL_Surface* loadedSurface = IMG_Load(utf8PathCStr);
    if (loadedSurface != nullptr) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL_image loaded '%s', attempting conversion to RGBA32.", utf8PathCStr);
        SDL_Surface* convertedSurface = SDL_ConvertSurfaceFormat(loadedSurface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loadedSurface); // Free original surface

        if (convertedSurface == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ConvertSurfaceFormat for '%s' failed: %s", utf8PathCStr, SDL_GetError());
            // Proceed to stb_image attempt
        }
        else {
            imageData.pixels = (unsigned char*)malloc(convertedSurface->w * convertedSurface->h * 4);
            if (imageData.pixels == nullptr) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to malloc for converted SDL_image pixels for '%s'.", utf8PathCStr);
                SDL_FreeSurface(convertedSurface);
                // Proceed to stb_image attempt
            }
            else {
                memcpy(imageData.pixels, convertedSurface->pixels, (size_t)convertedSurface->w * convertedSurface->h * 4);
                imageData.width = (unsigned int)convertedSurface->w;
                imageData.height = (unsigned int)convertedSurface->h;
                imageData.channels = 4; // RGBA32 means 4 channels
                SDL_FreeSurface(convertedSurface);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded '%s' with SDL_image and converted to RGBA.", utf8PathCStr);
                return imageData;
            }
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_image failed to load '%s': %s", utf8PathCStr, IMG_GetError());
    }

    // 3. stb_image Loading Attempt
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Attempting to load '%s' with stb_image (forcing RGBA)...", utf8PathCStr);
    int temp_w, temp_h, original_channels;
    // stbi_load returns pixels allocated by malloc, compatible with freeImageData's stbi_image_free.
    unsigned char* stb_pixels = stbi_load(utf8PathCStr, &temp_w, &temp_h, &original_channels, 4);
    if (stb_pixels != nullptr) {
        imageData.pixels = stb_pixels;
        imageData.width = (unsigned int)temp_w;
        imageData.height = (unsigned int)temp_h;
        imageData.channels = 4; // Forced 4 channels
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded '%s' with stb_image (forced RGBA). Original channels: %d", utf8PathCStr, original_channels);
        return imageData;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "stb_image failed to load '%s': %s", utf8PathCStr, stbi_failure_reason());
    }

    // 4. WIC Loading Attempt
    // Note: loadWithWIC takes wchar_t* imagePath
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Attempting to load '%ls' with WIC...", imagePath);
    if (loadWithWIC(imagePath, imageData)) { // loadWithWIC populates imageData and sets channels to 4
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded '%ls' with WIC.", imagePath);
        return imageData;
    }
    else {
        // loadWithWIC logs its own detailed errors.
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC failed to load '%ls'. Errors should have been logged by loadWithWIC.", imagePath);
    }

    // 5. All Failed
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "All image loading methods failed for '%ls'.", imagePath);
    return imageData; // Return empty imageData
}

// Helper function to convert wchar_t to char* (no longer directly used by loadImage, but might be by AVIF/WebP loaders)
// Caller must free the returned string using delete[]
char* wcharToCharPath(const wchar_t* wPath) {
    if (!wPath) return nullptr;
    size_t len = wcslen(wPath) + 1;
    size_t convertedChars = 0;
    char* cPath = new (std::nothrow) char[len * MB_CUR_MAX]; // Max bytes per char
    if (!cPath) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Memory allocation failed for char path conversion.");
        return nullptr;
    }
    errno_t err = wcstombs_s(&convertedChars, cPath, len * MB_CUR_MAX, wPath, _TRUNCATE);
    if (err != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Path conversion wchar_t to char failed: %d", err);
        delete[] cPath;
        return nullptr;
    }
    return cPath;
}


// Helper function to load images using WIC
bool loadWithWIC(const wchar_t* imagePath, ImageData& imageData) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { // RPC_E_CHANGED_MODE means COM already initialized in a compatible way.
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to initialize COM: %ld", hr);
        return false;
    }

    IWICImagingFactory* pFactory = nullptr;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory)
    );
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to create Imaging Factory: %ld", hr);
        if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) { // Check if CoUninitialize is needed
            CoUninitialize();
        }
        return false;
    }

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        imagePath,
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &pDecoder
    );
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to create Decoder from filename %ls: %ld", imagePath, hr);
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to get frame from Decoder: %ld", hr);
        if (pDecoder) pDecoder->Release();
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }

    // imageData.width and imageData.height are unsigned int, GetSize takes UINT*
    UINT tempW, tempH;
    hr = pFrame->GetSize(&tempW, &tempH);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to get image size: %ld", hr);
        if (pFrame) pFrame->Release();
        if (pDecoder) pDecoder->Release();
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }
    imageData.width = tempW;
    imageData.height = tempH;


    IWICFormatConverter* pConverter = nullptr;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to create Format Converter: %ld", hr);
        if (pFrame) pFrame->Release();
        if (pDecoder) pDecoder->Release();
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }

    hr = pConverter->Initialize(
        pFrame,
        GUID_WICPixelFormat32bppBGRA, // Request BGRA, then we will convert to RGBA
        WICBitmapDitherTypeNone,
        NULL,
        0.0,
        WICBitmapPaletteTypeMedianCut
    );
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to initialize Format Converter: %ld", hr);
        if (pConverter) pConverter->Release();
        if (pFrame) pFrame->Release();
        if (pDecoder) pDecoder->Release();
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }

    UINT stride = imageData.width * 4; // 4 bytes for 32bppBGRA
    UINT bufferSize = stride * imageData.height;

    // Use std::vector for temporary buffer to ensure RAII for its memory
    std::vector<BYTE> tempBufferVec(bufferSize);

    hr = pConverter->CopyPixels(NULL, stride, bufferSize, tempBufferVec.data());
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to copy pixels: %ld", hr);
        // tempBufferVec memory is managed by RAII
        if (pConverter) pConverter->Release();
        if (pFrame) pFrame->Release();
        if (pDecoder) pDecoder->Release();
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }

    // Allocate memory with malloc for stbi_image_free compatibility if needed by freeImageData
    // OR ensure freeImageData can handle pixels allocated differently (e.g. new unsigned char[])
    // The prompt implies stbi_image_free is used, so malloc is appropriate here.
    imageData.pixels = (unsigned char*)malloc(bufferSize);
    if (!imageData.pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WIC: Failed to allocate final pixel buffer with malloc.");
        // tempBufferVec memory is managed by RAII
        if (pConverter) pConverter->Release();
        if (pFrame) pFrame->Release();
        if (pDecoder) pDecoder->Release();
        if (pFactory) pFactory->Release();
        CoUninitialize();
        return false;
    }

    // Convert BGRA to RGBA while copying
    for (UINT i = 0; i < imageData.height; ++i) {
        for (UINT j = 0; j < imageData.width; ++j) {
            BYTE* srcPixel = tempBufferVec.data() + (i * stride) + (j * 4);
            unsigned char* dstPixel = imageData.pixels + (i * stride) + (j * 4);
            dstPixel[0] = srcPixel[2]; // Red
            dstPixel[1] = srcPixel[1]; // Green
            dstPixel[2] = srcPixel[0]; // Blue
            dstPixel[3] = srcPixel[3]; // Alpha
        }
    }

    imageData.channels = 4; // We converted to RGBA

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Successfully loaded image %ls with WIC (converted to RGBA).", imagePath);

    if (pConverter) pConverter->Release();
    if (pFrame) pFrame->Release();
    if (pDecoder) pDecoder->Release();
    if (pFactory) pFactory->Release();
    CoUninitialize();

    return true;
}

// Helper function to load images using libwebp
bool loadWithWebP(const wchar_t* imagePath, ImageData& imageData) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Attempting to load %ls with libwebp.", imagePath);
    imageData.pixels = nullptr; // Ensure pixels is null initially

    FILE* f = nullptr;
    errno_t err = _wfopen_s(&f, imagePath, L"rb");
    if (err != 0 || !f) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libwebp: Failed to open file %ls. Error: %d", imagePath, err);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libwebp: Invalid file size %ld for %ls.", fileSize, imagePath);
        fclose(f);
        return false;
    }

    std::vector<uint8_t> fileData(fileSize);
    if (fread(fileData.data(), 1, fileSize, f) != (size_t)fileSize) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libwebp: Failed to read file %ls into buffer.", imagePath);
        fclose(f);
        return false;
    }
    fclose(f);

    int width, height;
    if (!WebPGetInfo(fileData.data(), fileSize, &width, &height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libwebp: WebPGetInfo failed for %ls.", imagePath);
        return false;
    }

    // libwebp decodes to RGBA, so 4 channels. Use malloc for consistency if freeImageData uses stbi_image_free.
    imageData.pixels = (unsigned char*)malloc((size_t)width * height * 4);
    if (!imageData.pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libwebp: Failed to allocate memory for pixels for %ls.", imagePath);
        return false;
    }

    if (WebPDecodeRGBAInto(fileData.data(), fileSize, imageData.pixels, (size_t)width * height * 4, width * 4) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libwebp: WebPDecodeRGBAInto failed for %ls.", imagePath);
        free(imageData.pixels); // Use free as it was malloc'd
        imageData.pixels = nullptr;
        return false;
    }

    imageData.width = (unsigned int)width;
    imageData.height = (unsigned int)height;
    imageData.channels = 4;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Successfully loaded image %ls with libwebp.", imagePath);
    return true;
}

// Helper function to load images using libavif
bool loadWithAVIF(const wchar_t* imagePath, ImageData& imageData) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Attempting to load %ls with libavif.", imagePath);
    imageData.pixels = nullptr; // Ensure pixels is null initially

    char* charPath = wcharToCharPath(imagePath); // Uses the existing helper
    if (!charPath) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to convert image path to char* for %ls.", imagePath);
        return false;
    }

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to create decoder for %ls.", imagePath);
        delete[] charPath;
        return false;
    }

    avifResult result = avifDecoderSetIOFile(decoder, charPath);
    delete[] charPath; // Path is used by setIOFile, can be freed after.
    if (result != AVIF_RESULT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to set IO file for %ls. Error: %s", imagePath, avifResultToString(result));
        avifDecoderDestroy(decoder);
        return false;
    }

    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to parse AVIF data for %ls. Error: %s", imagePath, avifResultToString(result));
        avifDecoderDestroy(decoder);
        return false;
    }

    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to get next image for %ls. Error: %s", imagePath, avifResultToString(result));
        avifDecoderDestroy(decoder);
        return false;
    }

    imageData.width = decoder->image->width;
    imageData.height = decoder->image->height;
    imageData.channels = 4; // We will convert to RGBA

    size_t bufferSize = (size_t)imageData.width * imageData.height * imageData.channels;
    // Use malloc for consistency if freeImageData uses stbi_image_free.
    imageData.pixels = (unsigned char*)malloc(bufferSize);
    if (!imageData.pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to allocate memory for pixels for %ls.", imagePath);
        avifDecoderDestroy(decoder);
        return false;
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    rgb.pixels = imageData.pixels;
    rgb.rowBytes = imageData.width * imageData.channels;

    result = avifImageYUVToRGB(decoder->image, &rgb);
    if (result != AVIF_RESULT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "libavif: Failed to convert YUV to RGB for %ls. Error: %s", imagePath, avifResultToString(result));
        free(imageData.pixels); // Use free as it was malloc'd
        imageData.pixels = nullptr;
        avifDecoderDestroy(decoder);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Successfully loaded image %ls with libavif.", imagePath);
    avifDecoderDestroy(decoder);
    return true;
}


void displayImage(SDL_Renderer* renderer, ImageData* imageData, SDL_Rect destinationRect) {
    if (!imageData || !imageData->pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "displayImage: imageData or imageData->pixels is null.");
        return;
    }

    Uint32 sdlPixelFormat;
    int pitch;

    if (imageData->channels == 3) {
        sdlPixelFormat = SDL_PIXELFORMAT_RGB24;
        pitch = imageData->width * 3;
    }
    else if (imageData->channels == 4) {
        sdlPixelFormat = SDL_PIXELFORMAT_RGBA32; // All loaders now aim for RGBA
        pitch = imageData->width * 4;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "displayImage: Unsupported number of channels: %d. Expected 3 or 4.", imageData->channels);
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        imageData->pixels,
        imageData->width,
        imageData->height,
        imageData->channels * 8,
        pitch,
        (imageData->channels == 4) ? 0x000000FF : 0x00FF0000, // Rmask (RGBA or RGB)
        (imageData->channels == 4) ? 0x0000FF00 : 0x0000FF00, // Gmask (RGBA or RGB)
        (imageData->channels == 4) ? 0x00FF0000 : 0x000000FF, // Bmask (RGBA or RGB)
        (imageData->channels == 4) ? 0xFF000000 : 0           // Amask (RGBA or none for RGB)
    );

    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "displayImage: Failed to create SDL_Surface: %s", SDL_GetError());
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "displayImage: Failed to create SDL_Texture: %s", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    SDL_RenderCopy(renderer, texture, NULL, &destinationRect);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void freeImageData(ImageData* imageData) {
    if (imageData && imageData->pixels) {
        // Assuming stbi_load was the source for some paths, stbi_image_free is correct.
        // For SDL_image and WIC, if we used malloc, then free() is correct.
        // stbi_image_free is typically just a wrapper around free() unless specific
        // STB_IMAGE options change allocation, which we are not using.
        // So, stbi_image_free should be safe for malloc'd data.
        // If loadWebP or loadAVIF used malloc, stbi_image_free is also fine.
        stbi_image_free(imageData->pixels);
        imageData->pixels = nullptr;
        imageData->width = 0;
        imageData->height = 0;
        imageData->channels = 0;
    }
}




bool saveImage(ImageData* imageData, const wchar_t* outputPath, ImageSaveFormat format, int jpegQuality) {
    // Convert wchar_t outputPath to std::string for file operations
    // This part needs robust conversion similar to what's in loadImage now.
    std::string utf8_path_str;
    if (outputPath) {
        size_t pathLen = wcslen(outputPath);
        if (pathLen > 0) {
            std::vector<char> utf8PathVec(pathLen * 4 + 1);
            size_t convertedChars = 0;
            errno_t err = wcstombs_s(&convertedChars, utf8PathVec.data(), utf8PathVec.size(), outputPath, _TRUNCATE);
            if (err == 0 && convertedChars > 0) {
                utf8_path_str = utf8PathVec.data();
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage: Failed to convert outputPath %ls to UTF-8. Error: %d", outputPath, err);
                return false;
            }
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage: Empty outputPath provided.");
            return false;
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage: NULL outputPath provided.");
        return false;
    }

    if (!imageData || !imageData->pixels || imageData->width == 0 || imageData->height == 0 || imageData->channels != 4) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage: Invalid image data. Ensure it's loaded and is RGBA (4 channels).");
        return false;
    }

    int success = 0;
    if (format == SAVE_FORMAT_PNG) {
        success = stbi_write_png(utf8_path_str.c_str(), imageData->width, imageData->height, imageData->channels, imageData->pixels, imageData->width * imageData->channels);
    }
    else if (format == SAVE_FORMAT_BMP) {
        success = stbi_write_bmp(utf8_path_str.c_str(), imageData->width, imageData->height, imageData->channels, imageData->pixels);
    }
    else if (format == SAVE_FORMAT_TGA) {
        success = stbi_write_tga(utf8_path_str.c_str(), imageData->width, imageData->height, imageData->channels, imageData->pixels);
    }
    else if (format == SAVE_FORMAT_JPG) {
        success = stbi_write_jpg(utf8_path_str.c_str(), imageData->width, imageData->height, imageData->channels, imageData->pixels, jpegQuality);
    }
    else if (format == SAVE_FORMAT_WEBP) {
        uint8_t* output = nullptr;
        // Assuming quality is roughly 0-100, map to WebP's float quality (e.g., 75.0f)
        float webp_quality = (jpegQuality > 0 && jpegQuality <= 100) ? (float)jpegQuality : 75.0f;
        size_t output_size = WebPEncodeRGBA(imageData->pixels, imageData->width, imageData->height, imageData->width * imageData->channels, webp_quality, &output);
        if (output_size > 0 && output) {
            FILE* file = nullptr;
            errno_t err = fopen_s(&file, utf8_path_str.c_str(), "wb");
            if (err == 0 && file) {
                fwrite(output, 1, output_size, file);
                fclose(file);
                success = 1; // Mark as success
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (WebP): Failed to open file '%s' for writing. Error: %d", utf8_path_str.c_str(), err);
            }
            WebPFree(output);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (WebP): WebPEncodeRGBA failed for '%s'.", utf8_path_str.c_str());
        }
    }
    else if (format == SAVE_FORMAT_AVIF) {
        // AVIF saving is more complex, this is a simplified placeholder matching stbi_write style.
        // Proper AVIF encoding would involve setting up encoder parameters (quality, speed, etc.)
        avifImage* avif = avifImageCreate(imageData->width, imageData->height, 8, AVIF_PIXEL_FORMAT_YUV444); // Or other preferred YUV format
        if (!avif) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (AVIF): avifImageCreate failed for '%s'.", utf8_path_str.c_str());
            return false;
        }

        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, avif);
        rgb.format = AVIF_RGB_FORMAT_RGBA; // Input is RGBA
        rgb.pixels = imageData->pixels;
        rgb.rowBytes = imageData->width * imageData->channels;

        avifResult convResult = avifImageRGBToYUV(avif, &rgb);
        if (convResult != AVIF_RESULT_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (AVIF): avifImageRGBToYUV failed for '%s': %s", utf8_path_str.c_str(), avifResultToString(convResult));
            avifImageDestroy(avif);
            return false;
        }

        avifEncoder* encoder = avifEncoderCreate();
        if (!encoder) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (AVIF): avifEncoderCreate failed for '%s'.", utf8_path_str.c_str());
            avifImageDestroy(avif);
            return false;
        }
        // Set quality (0-100 for libavif, maps to min/max quantizer)
        // Lower value = higher quality. 60 is often default/lossless in examples.
        // For lossy: 0-100, where 100 is best quality for some interpretations, or 0 for others.
        // Let's map jpegQuality (1-100, 100=best) to avif quantizer (0-63, 0=lossless, 63=worst)
        // This is a rough mapping; specific tuning is needed for desired results.
        int avif_quality = AVIF_QUANTIZER_BEST_QUALITY; // Default to lossless (0)
        if (jpegQuality > 0 && jpegQuality < 100) { // Lossy
            avif_quality = ((100 - jpegQuality) * AVIF_QUANTIZER_WORST_QUALITY) / 100;
        }
        encoder->minQuantizer = avif_quality;
        encoder->maxQuantizer = avif_quality;
        // encoder->speed = AVIF_SPEED_DEFAULT; // Or other speed settings

        avifRWData output = AVIF_DATA_EMPTY;
        avifResult writeResult = avifEncoderWrite(encoder, avif, &output);

        if (writeResult == AVIF_RESULT_OK && output.size > 0) {
            FILE* file = nullptr;
            errno_t err = fopen_s(&file, utf8_path_str.c_str(), "wb");
            if (err == 0 && file) {
                fwrite(output.data, 1, output.size, file);
                fclose(file);
                success = 1; // Mark as success
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (AVIF): Failed to open file '%s' for writing. Error: %d", utf8_path_str.c_str(), err);
            }
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage (AVIF): avifEncoderWrite failed for '%s': %s", utf8_path_str.c_str(), avifResultToString(writeResult));
        }

        avifRWDataFree(&output);
        avifEncoderDestroy(encoder);
        avifImageDestroy(avif);
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "saveImage: Unsupported format specified for '%s'.", utf8_path_str.c_str());
        return false;
    }

    if (success) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Successfully saved image to '%s'.", utf8_path_str.c_str());
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save image to '%s' using STB/WebP/AVIF.", utf8_path_str.c_str());
    }
    return success != 0;
}

