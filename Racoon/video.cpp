#include "Header.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <algorithm>
#include <string>

#ifdef min
#undef min
#endif

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

bool initializeFFmpeg() {
    logError("FFmpeg initialized.");
    return true;
}

bool openVideoFile(const char* filePath, VideoContext& videoCtx) {
    // 1. Initialize AVFormatContext
    if (avformat_open_input(&videoCtx.formatContext, filePath, nullptr, nullptr) != 0) {
        logError("FFmpeg: ERROR could not open video file %s", filePath);
        return false;
    }

    // 2. Find stream information
    if (avformat_find_stream_info(videoCtx.formatContext, nullptr) < 0) {
        logError("FFmpeg: ERROR could not find stream info for %s", filePath);
        closeVideoFile(videoCtx);
        return false;
    }

    // 3. Find Video and Audio Streams
    videoCtx.videoStreamIndex = -1;
    videoCtx.audioStreamIndex = -1;
    for (unsigned int i = 0; i < videoCtx.formatContext->nb_streams; i++) {
        AVCodecParameters* pCodecParams = videoCtx.formatContext->streams[i]->codecpar;
        if (pCodecParams->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (videoCtx.videoStreamIndex == -1) videoCtx.videoStreamIndex = i;
        }
        else if (pCodecParams->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (videoCtx.audioStreamIndex == -1) videoCtx.audioStreamIndex = i;
        }

        const AVCodec* pVideoCodec = avcodec_find_decoder_by_name("hevc_dxva2"); // Use DXVA2 for HEVC
        if (!pVideoCodec) {
            pVideoCodec = avcodec_find_decoder(pCodecParams->codec_id); // Use pCodecParams instead of pVideoCodecParams
        }
    }

    if (videoCtx.videoStreamIndex == -1) {
        logError("FFmpeg: ERROR video stream not found for %s", filePath);
        closeVideoFile(videoCtx);
        return false;
    }

    // 4. Initialize Video Codec
    AVCodecParameters* pVideoCodecParams = videoCtx.formatContext->streams[videoCtx.videoStreamIndex]->codecpar;
    const AVCodec* pVideoCodec = avcodec_find_decoder(pVideoCodecParams->codec_id);
    if (!pVideoCodec) {
        logError("FFmpeg: ERROR video codec not found for %s", filePath);
        closeVideoFile(videoCtx);
        return false;
    }
    videoCtx.videoCodecContext = avcodec_alloc_context3(pVideoCodec);
    if (!videoCtx.videoCodecContext) {
        logError("FFmpeg: ERROR could not allocate video codec context for %s", filePath);
        closeVideoFile(videoCtx);
        return false;
    }
    if (avcodec_parameters_to_context(videoCtx.videoCodecContext, pVideoCodecParams) < 0) {
        logError("FFmpeg: ERROR could not copy video codec params for %s", filePath);
        closeVideoFile(videoCtx);
        return false;
    }
    if (avcodec_open2(videoCtx.videoCodecContext, pVideoCodec, nullptr) < 0) {
        logError("FFmpeg: ERROR could not open video codec for %s", filePath);
        closeVideoFile(videoCtx);
        return false;
    }
    logError("FFmpeg: Video codec initialized: %s, Resolution: %dx%d", avcodec_get_name(pVideoCodec->id), videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height);

    // 5. Initialize Audio Codec & SDL Audio Device
    if (videoCtx.audioStreamIndex != -1) {
        AVCodecParameters* pAudioCodecParams = videoCtx.formatContext->streams[videoCtx.audioStreamIndex]->codecpar;
        const AVCodec* pAudioCodec = avcodec_find_decoder(pAudioCodecParams->codec_id);
        if (pAudioCodec) {
            videoCtx.audioCodecContext = avcodec_alloc_context3(pAudioCodec);
            if (!videoCtx.audioCodecContext) {
                logError("FFmpeg: WARN could not allocate audio codec context for %s", filePath);
            }
            else {
                if (avcodec_parameters_to_context(videoCtx.audioCodecContext, pAudioCodecParams) < 0) {
                    logError("FFmpeg: WARN could not copy audio codec parameters for %s", filePath);
                    avcodec_free_context(&videoCtx.audioCodecContext);
                }
                else {
                    if (avcodec_open2(videoCtx.audioCodecContext, pAudioCodec, nullptr) < 0) {
                        logError("FFmpeg: WARN could not open audio codec for %s", filePath);
                        avcodec_free_context(&videoCtx.audioCodecContext);
                    }
                    else {
                        av_channel_layout_copy(&videoCtx.audioChannelLayout, &pAudioCodecParams->ch_layout);
                        logError("FFmpeg: Audio codec initialized: %s, Sample Rate: %d, Channels: %d, Format: %s",
                            avcodec_get_name(pAudioCodec->id),
                            static_cast<int>(pAudioCodecParams->sample_rate),
                            static_cast<int>(pAudioCodecParams->ch_layout.nb_channels),
                            av_get_sample_fmt_name(static_cast<AVSampleFormat>(pAudioCodecParams->format)));

                        // Initialize SDL Audio Device
                        SDL_memset(&videoCtx.desiredAudioSpec, 0, sizeof(SDL_AudioSpec));
                        videoCtx.desiredAudioSpec.freq = pAudioCodecParams->sample_rate;
                        videoCtx.desiredAudioSpec.format = AUDIO_S16SYS;
                        videoCtx.desiredAudioSpec.channels = 2; // Stereo
                        videoCtx.desiredAudioSpec.samples = 8192; // Increased buffer size
                        videoCtx.desiredAudioSpec.callback = audioCallback;
                        videoCtx.desiredAudioSpec.userdata = &videoCtx;

                        videoCtx.audioDevice = SDL_OpenAudioDevice(nullptr, 0, &videoCtx.desiredAudioSpec, &videoCtx.obtainedAudioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
                        if (videoCtx.audioDevice == 0) {
                            logError("FFmpeg: WARN Failed to open SDL audio device: %s", SDL_GetError());
                            avcodec_free_context(&videoCtx.audioCodecContext);
                        }
                        else {
                            logError("FFmpeg: SDL audio device opened. Obtained: freq=%d, channels=%d, format=%d",
                                videoCtx.obtainedAudioSpec.freq, videoCtx.obtainedAudioSpec.channels, videoCtx.obtainedAudioSpec.format);
                            SDL_PauseAudioDevice(videoCtx.audioDevice, 0); // Start playback
                        }

                        videoCtx.decodedAudioFrame = av_frame_alloc();
                        videoCtx.audioPacket = av_packet_alloc();
                        if (!videoCtx.decodedAudioFrame || !videoCtx.audioPacket) {
                            logError("FFmpeg: Failed to allocate audio frame/packet");
                            av_frame_free(&videoCtx.decodedAudioFrame);
                            av_packet_free(&videoCtx.audioPacket);
                            avcodec_free_context(&videoCtx.audioCodecContext);
                            if (videoCtx.audioDevice) {
                                SDL_CloseAudioDevice(videoCtx.audioDevice);
                                videoCtx.audioDevice = 0;
                            }
                        }
                        else {
                            // Allocate audio buffer
                            videoCtx.audioBufferAllocatedSize = 8192 * videoCtx.obtainedAudioSpec.channels * 2; // 2 bytes per sample for S16
                            videoCtx.audioBuffer = static_cast<uint8_t*>(av_malloc(videoCtx.audioBufferAllocatedSize));
                            if (!videoCtx.audioBuffer) {
                                logError("FFmpeg: WARN Failed to allocate audio buffer");
                                av_frame_free(&videoCtx.decodedAudioFrame);
                                av_packet_free(&videoCtx.audioPacket);
                                avcodec_free_context(&videoCtx.audioCodecContext);
                                if (videoCtx.audioDevice) {
                                    SDL_CloseAudioDevice(videoCtx.audioDevice);
                                    videoCtx.audioDevice = 0;
                                }
                            }
                            else {
                                videoCtx.audioBufferSize = 0;
                                videoCtx.audioBufferPos = 0;

                                // Check if resampling is needed
                                bool needsResampling = pAudioCodecParams->format != AV_SAMPLE_FMT_S16 ||
                                    pAudioCodecParams->ch_layout.nb_channels != videoCtx.obtainedAudioSpec.channels ||
                                    pAudioCodecParams->sample_rate != videoCtx.obtainedAudioSpec.freq;

                                if (!needsResampling) {
                                    logError("FFmpeg: Audio format matches SDL spec (S16, %d channels, %d Hz). Skipping SwrContext.",
                                        videoCtx.obtainedAudioSpec.channels, videoCtx.obtainedAudioSpec.freq);
                                }
                                else {
                                    // Setup SwrContext for resampling to S16
                                    videoCtx.swrContext = swr_alloc();
                                    if (!videoCtx.swrContext) {
                                        logError("FFmpeg: WARN Cannot allocate SwrContext. Audio may not play.");
                                        av_frame_free(&videoCtx.decodedAudioFrame);
                                        av_packet_free(&videoCtx.audioPacket);
                                        avcodec_free_context(&videoCtx.audioCodecContext);
                                        av_free(videoCtx.audioBuffer);
                                        videoCtx.audioBuffer = nullptr;
                                        if (videoCtx.audioDevice) {
                                            SDL_CloseAudioDevice(videoCtx.audioDevice);
                                            videoCtx.audioDevice = 0;
                                        }
                                    }
                                    else {
                                        AVChannelLayout in_ch_layout;
                                        AVChannelLayout out_ch_layout;
                                        av_channel_layout_copy(&in_ch_layout, &videoCtx.audioChannelLayout);
                                        av_channel_layout_default(&out_ch_layout, videoCtx.obtainedAudioSpec.channels); // Use SDL channels

                                        char in_layout_desc[128], out_layout_desc[128];
                                        av_channel_layout_describe(&in_ch_layout, in_layout_desc, sizeof(in_layout_desc));
                                        av_channel_layout_describe(&out_ch_layout, out_layout_desc, sizeof(out_layout_desc));
                                        logError("FFmpeg: SwrContext options: in_ch_layout: %s (%d channels), in_sample_rate: %d, in_sample_fmt: %s, out_ch_layout: %s (%d channels), out_sample_rate: %d, out_sample_fmt: %s",
                                            in_layout_desc, in_ch_layout.nb_channels, pAudioCodecParams->sample_rate, av_get_sample_fmt_name(static_cast<AVSampleFormat>(pAudioCodecParams->format)),
                                            out_layout_desc, out_ch_layout.nb_channels, videoCtx.obtainedAudioSpec.freq, av_get_sample_fmt_name(AV_SAMPLE_FMT_S16));

                                        // Set SwrContext options
                                        swr_alloc_set_opts2(&videoCtx.swrContext,
                                            &out_ch_layout, AV_SAMPLE_FMT_S16, videoCtx.obtainedAudioSpec.freq,
                                            &in_ch_layout, static_cast<AVSampleFormat>(pAudioCodecParams->format), pAudioCodecParams->sample_rate,
                                            0, nullptr);

                                        int swr_init_ret = swr_init(videoCtx.swrContext);
                                        if (swr_init_ret < 0) {
                                            char errBuf[AV_ERROR_MAX_STRING_SIZE];
                                            av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, swr_init_ret);
                                            logError("FFmpeg: WARN Cannot initialize SwrContext. Error: %s. Audio may not play.", errBuf);
                                            swr_free(&videoCtx.swrContext);
                                            av_frame_free(&videoCtx.decodedAudioFrame);
                                            av_packet_free(&videoCtx.audioPacket);
                                            avcodec_free_context(&videoCtx.audioCodecContext);
                                            av_free(videoCtx.audioBuffer);
                                            videoCtx.audioBuffer = nullptr;
                                            if (videoCtx.audioDevice) {
                                                SDL_CloseAudioDevice(videoCtx.audioDevice);
                                                videoCtx.audioDevice = 0;
                                            }
                                            av_channel_layout_uninit(&in_ch_layout);
                                            av_channel_layout_uninit(&out_ch_layout);
                                            return true; // Continue with video
                                        }
                                        else {
                                            logError("FFmpeg: Audio SwrContext initialized for S16 conversion.");
                                        }

                                        av_channel_layout_uninit(&in_ch_layout);
                                        av_channel_layout_uninit(&out_ch_layout);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else {
            logError("FFmpeg: WARN audio codec not found for %s", filePath);
        }
    }

    // 6. Initialize SwsContext for video frame conversion with SWS_BICUBIC
    if (videoCtx.videoCodecContext) {
        videoCtx.swsContext = sws_getContext(
            videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height, videoCtx.videoCodecContext->pix_fmt,
            videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height, AV_PIX_FMT_RGBA,
            SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!videoCtx.swsContext) {
            logError("FFmpeg: ERROR Could not initialize SwsContext for video conversion.");
            closeVideoFile(videoCtx);
            return false;
        }
    }

    // 7. Allocate Video Frames
    videoCtx.decodedFrame = av_frame_alloc();
    if (!videoCtx.decodedFrame) {
        logError("FFmpeg: ERROR Could not allocate video decodedFrame.");
        closeVideoFile(videoCtx);
        return false;
    }
    videoCtx.rgbFrame = av_frame_alloc();
    if (!videoCtx.rgbFrame) {
        logError("FFmpeg: ERROR Could not allocate video rgbFrame.");
        closeVideoFile(videoCtx);
        return false;
    }

    if (videoCtx.videoCodecContext) {
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height, 1);
        videoCtx.rgbBuffer = static_cast<uint8_t*>(av_malloc(numBytes * sizeof(uint8_t)));
        if (!videoCtx.rgbBuffer) {
            logError("FFmpeg: ERROR Could not allocate RGB buffer.");
            closeVideoFile(videoCtx);
            return false;
        }
        av_image_fill_arrays(videoCtx.rgbFrame->data, videoCtx.rgbFrame->linesize, videoCtx.rgbBuffer, AV_PIX_FMT_RGBA,
            videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height, 1);
    }

    logError("FFmpeg: Successfully opened and configured video %s", filePath);
    return true;
}

void closeVideoFile(VideoContext& videoCtx) {
    logError("FFmpeg: Closing video file.");

    if (videoCtx.audioDevice != 0) {
        SDL_PauseAudioDevice(videoCtx.audioDevice, 1);
        SDL_CloseAudioDevice(videoCtx.audioDevice);
        videoCtx.audioDevice = 0;
        logError("FFmpeg: Closed SDL Audio Device.");
    }

    av_frame_free(&videoCtx.decodedAudioFrame);
    av_packet_free(&videoCtx.audioPacket);
    av_free(videoCtx.audioBuffer);
    videoCtx.audioBuffer = nullptr;
    videoCtx.audioBufferSize = 0;
    videoCtx.audioBufferPos = 0;
    videoCtx.audioBufferAllocatedSize = 0;

    swr_free(&videoCtx.swrContext);
    av_frame_free(&videoCtx.decodedFrame);
    av_frame_free(&videoCtx.rgbFrame);
    av_free(videoCtx.rgbBuffer);
    videoCtx.rgbBuffer = nullptr;
    sws_freeContext(videoCtx.swsContext);
    avcodec_free_context(&videoCtx.videoCodecContext);
    avcodec_free_context(&videoCtx.audioCodecContext);
    avformat_close_input(&videoCtx.formatContext);
    av_channel_layout_uninit(&videoCtx.audioChannelLayout);

    videoCtx.formatContext = nullptr;
    videoCtx.videoCodecContext = nullptr;
    videoCtx.audioCodecContext = nullptr;
    videoCtx.videoStreamIndex = -1;
    videoCtx.audioStreamIndex = -1;
    videoCtx.swsContext = nullptr;
    videoCtx.decodedFrame = nullptr;
    videoCtx.rgbFrame = nullptr;
    videoCtx.audioPacket = nullptr;
    videoCtx.decodedAudioFrame = nullptr;

    logError("FFmpeg: Video file closed and context reset.");
}

bool decodeNextAudioPacket(VideoContext& videoCtx) {
    logError("FFmpeg: decodeNextAudioPacket entered. Audio Buffer Size: %u, Pos: %u, Allocated: %u", videoCtx.audioBufferSize, videoCtx.audioBufferPos, videoCtx.audioBufferAllocatedSize);
    if (!videoCtx.formatContext || !videoCtx.audioCodecContext || !videoCtx.audioPacket || !videoCtx.decodedAudioFrame || !videoCtx.audioBuffer) {
        logError("FFmpeg: decodeNextAudioPacket returning false due to null context members.");
        return false;
    }

    AVPacket* packet = videoCtx.audioPacket;
    int ret;

    while (true) {
        ret = avcodec_receive_frame(videoCtx.audioCodecContext, videoCtx.decodedAudioFrame);
        if (ret == 0) {
            logError("FFmpeg: Successfully received audio frame (prior to reading new packet).");
        }
        else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            if (ret == AVERROR_EOF) {
                logError("FFmpeg: avcodec_receive_frame returned AVERROR_EOF before reading new packet. Decoder fully flushed.");
                return false;
            }

            logError("FFmpeg: avcodec_receive_frame returned EAGAIN. Attempting to read new packet.");
            av_packet_unref(packet);
            ret = av_read_frame(videoCtx.formatContext, packet);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    logError("FFmpeg: av_read_frame reached EOF (audio). Sending NULL packet to flush decoder.");
                    ret = avcodec_send_packet(videoCtx.audioCodecContext, nullptr);
                    if (ret < 0) {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE];
                        av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                        logError("FFmpeg: Error sending NULL flush packet to audio decoder: %s (code %d)", errBuf, ret);
                        return false;
                    }
                    continue;
                }
                else {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                    logError("FFmpeg: av_read_frame error (audio): %s (code %d)", errBuf, ret);
                    return false;
                }
            }

            if (packet->stream_index != videoCtx.audioStreamIndex) {
                av_packet_unref(packet);
                continue;
            }

            logError("FFmpeg: Processing an audio packet from stream %d. Size: %d, PTS: %lld", packet->stream_index, packet->size, packet->pts);
            ret = avcodec_send_packet(videoCtx.audioCodecContext, packet);
            if (ret < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                logError("FFmpeg: Error sending audio packet to decoder: %s (code %d)", errBuf, ret);
                av_packet_unref(packet);
                if (ret == AVERROR(EAGAIN)) {
                    logError("FFmpeg: avcodec_send_packet returned EAGAIN unexpectedly, continuing.");
                    continue;
                }
                return false;
            }
            av_packet_unref(packet);
            continue;
        }
        else {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
            logError("FFmpeg: Error receiving audio frame from decoder: %s (code %d)", errBuf, ret);
            return false;
        }

        uint8_t* out_buffer_ptr[1] = { videoCtx.audioBuffer };
        int out_samples = 0;

        if (videoCtx.swrContext) {
            logError("FFmpeg: swr_convert input: nb_samples=%d, sample_rate=%d, format=%s, ch_layout_nb_channels=%d",
                videoCtx.decodedAudioFrame->nb_samples, videoCtx.decodedAudioFrame->sample_rate,
                av_get_sample_fmt_name(static_cast<AVSampleFormat>(videoCtx.decodedAudioFrame->format)),
                videoCtx.decodedAudioFrame->ch_layout.nb_channels);

            int max_out_samples = av_rescale_rnd(videoCtx.decodedAudioFrame->nb_samples, videoCtx.obtainedAudioSpec.freq, videoCtx.decodedAudioFrame->sample_rate, AV_ROUND_UP);
            uint32_t required_buffer_size = static_cast<uint32_t>(max_out_samples * videoCtx.obtainedAudioSpec.channels * 2);

            if (videoCtx.audioBufferAllocatedSize < required_buffer_size) {
                logError("FFmpeg: Reallocating audioBuffer. Old size: %u, New size: %u", videoCtx.audioBufferAllocatedSize, required_buffer_size);
                av_free(videoCtx.audioBuffer);
                videoCtx.audioBuffer = static_cast<uint8_t*>(av_malloc(required_buffer_size));
                if (!videoCtx.audioBuffer) {
                    logError("FFmpeg: Failed to reallocate audio buffer");
                    videoCtx.audioBufferAllocatedSize = 0;
                    return false;
                }
                videoCtx.audioBufferAllocatedSize = required_buffer_size;
            }

            out_samples = swr_convert(videoCtx.swrContext, out_buffer_ptr, max_out_samples,
                (const uint8_t**)videoCtx.decodedAudioFrame->extended_data, videoCtx.decodedAudioFrame->nb_samples);

            if (out_samples < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, out_samples);
                logError("FFmpeg: swr_convert error: %s (code %d)", errBuf, out_samples);
                return false;
            }
            videoCtx.audioBufferSize = out_samples * videoCtx.obtainedAudioSpec.channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            logError("FFmpeg: swr_convert output: %d samples, calculated audioBufferSize: %u", out_samples, videoCtx.audioBufferSize);
        }
        else {
            int data_size = av_samples_get_buffer_size(nullptr, videoCtx.decodedAudioFrame->ch_layout.nb_channels,
                videoCtx.decodedAudioFrame->nb_samples, static_cast<AVSampleFormat>(videoCtx.decodedAudioFrame->format), 1);
            logError("FFmpeg: Direct copy attempt. Calculated data_size: %d, channels: %d, samples: %d, format: %s",
                data_size, videoCtx.decodedAudioFrame->ch_layout.nb_channels, videoCtx.decodedAudioFrame->nb_samples,
                av_get_sample_fmt_name(static_cast<AVSampleFormat>(videoCtx.decodedAudioFrame->format)));

            if (data_size > 0 && static_cast<uint32_t>(data_size) <= videoCtx.audioBufferAllocatedSize &&
                videoCtx.decodedAudioFrame->format == AV_SAMPLE_FMT_S16 &&
                videoCtx.decodedAudioFrame->ch_layout.nb_channels == videoCtx.obtainedAudioSpec.channels &&
                videoCtx.decodedAudioFrame->sample_rate == videoCtx.obtainedAudioSpec.freq) {
                memcpy(videoCtx.audioBuffer, videoCtx.decodedAudioFrame->data[0], data_size);
                videoCtx.audioBufferSize = data_size;
            }
            else {
                logError("FFmpeg: Cannot copy audio directly. Format mismatch or buffer too small. Decoded: format=%s, channels=%d, rate=%d. SDL: format=AUDIO_S16SYS, channels=%d, rate=%d",
                    av_get_sample_fmt_name(static_cast<AVSampleFormat>(videoCtx.decodedAudioFrame->format)),
                    videoCtx.decodedAudioFrame->ch_layout.nb_channels, videoCtx.decodedAudioFrame->sample_rate,
                    videoCtx.obtainedAudioSpec.channels, videoCtx.obtainedAudioSpec.freq);
                videoCtx.audioBufferSize = 0;
            }
        }

        videoCtx.audioBufferPos = 0;
        av_frame_unref(videoCtx.decodedAudioFrame);
        logError("FFmpeg: decodeNextAudioPacket returning true (successfully decoded and processed a frame). audioBufferSize: %u", videoCtx.audioBufferSize);
        return true;
    }
    logError("FFmpeg: decodeNextAudioPacket returning false (exited loop unexpectedly).");
    return false;
}

 

void toggle_fullscreen(VideoContext& videoCtx, SDL_Window* window, SDL_Renderer* renderer) {
    videoCtx.is_fullscreen = !videoCtx.is_fullscreen;
    Uint32 flags = videoCtx.is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    SDL_SetWindowFullscreen(window, flags);

    int screen_width = 0; // Declare screen_width
    int screen_height = 0; // Declare screenHeight

    SDL_GetWindowSize(window, &screen_width, &screen_height);

    float aspect_ratio = (float)videoCtx.videoCodecContext->width / videoCtx.videoCodecContext->height;
    int render_width = screen_width;
    int render_height = (int)(screen_width / aspect_ratio);

    if (render_height > screen_height) {
        render_height = screen_height;
        render_width = (int)(screen_height * aspect_ratio);
    }

    SDL_Rect dst_rect = { (screen_width - render_width) / 2, (screen_height - render_height) / 2, render_width, render_height };
    SDL_RenderSetViewport(renderer, &dst_rect); // Corrected function
    logError("SDL: Toggled fullscreen: %s=%s, viewport x=%d, y=%d, w=%d, h=%d",
        videoCtx.is_fullscreen ? "true" : "false", dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
}

bool decodeVideoFrame(VideoContext& videoCtx, SDL_Texture** videoTexture, SDL_Renderer* renderer) {
    if (!videoCtx.formatContext || !videoCtx.videoCodecContext || !videoCtx.swsContext) {
        logError("FFmpeg: Invalid video context for decoding.");
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        logError("FFmpeg: Failed to allocate AVPacket.");
        return false;
    }

    while (av_read_frame(videoCtx.formatContext, packet) >= 0) {
        if (packet->stream_index == videoCtx.videoStreamIndex) {
            int retries = 0;
            const int max_retries = 10;
            while (retries < max_retries) {
                int ret = avcodec_send_packet(videoCtx.videoCodecContext, packet);
                if (ret == 0) {
                    break; // Packet sent successfully
                }
                else if (ret == AVERROR(EAGAIN)) {
                    logError("FFmpeg: Video decoder busy, retrying (%d/%d).", retries + 1, max_retries);
                    SDL_Delay(5); // Increased delay to 5ms
                    retries++;
                    continue;
                }
                else {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                    logError("FFmpeg: Error sending video packet to decoder: %s", errBuf);
                    av_packet_unref(packet);
                    av_packet_free(&packet);
                    return false;
                }
            }
            if (retries >= max_retries) {
                logError("FFmpeg: Video decoder busy after %d retries, skipping packet.", max_retries);
                av_packet_unref(packet);
                continue;
            }

            int ret = avcodec_receive_frame(videoCtx.videoCodecContext, videoCtx.decodedFrame);
            if (ret == 0) {
                sws_scale(videoCtx.swsContext,
                    (const uint8_t* const*)videoCtx.decodedFrame->data, videoCtx.decodedFrame->linesize,
                    0, videoCtx.videoCodecContext->height,
                    videoCtx.rgbFrame->data, videoCtx.rgbFrame->linesize);

                if (renderer && videoTexture) {
                    if (*videoTexture == nullptr ||
                        [&]() {
                            int w, h;
                            SDL_QueryTexture(*videoTexture, nullptr, nullptr, &w, &h);
                            return w != videoCtx.videoCodecContext->width || h != videoCtx.videoCodecContext->height;
                        }()) {
                        if (*videoTexture) SDL_DestroyTexture(*videoTexture);
                        *videoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STREAMING,
                            videoCtx.videoCodecContext->width,
                            videoCtx.videoCodecContext->height);
                        if (!*videoTexture) {
                            logError("FFmpeg: Failed to create SDL_Texture for video: %s", SDL_GetError());
                            av_packet_unref(packet);
                            av_packet_free(&packet);
                            return false;
                        }
                    }

                    if (SDL_UpdateTexture(*videoTexture, nullptr, videoCtx.rgbBuffer, videoCtx.rgbFrame->linesize[0]) != 0) {
                        logError("FFmpeg: Failed to update SDL_Texture for video: %s", SDL_GetError());
                    }
                }
                av_packet_unref(packet);
                av_packet_free(&packet);
                return true;
            }
            else if (ret == AVERROR(EAGAIN)) {
                av_packet_unref(packet);
                continue;
            }
            else if (ret == AVERROR_EOF) {
                logError("FFmpeg: Video decoder flushed, EOF reached.");
                av_packet_unref(packet);
                av_packet_free(&packet);
                return false;
            }
            else {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                logError("FFmpeg: Error receiving video frame from decoder: %s", errBuf);
                av_packet_unref(packet);
                av_packet_free(&packet);
                return false;
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return false;
}






    extern "C" void audioCallback(void* userdata, uint8_t * stream, int len) {
        VideoContext* videoCtx = static_cast<VideoContext*>(userdata);
        logError("FFmpeg: audioCallback entered. Requested len: %d, Current audioBufferPos: %u, audioBufferSize: %u",
            len, videoCtx ? videoCtx->audioBufferPos : -1, videoCtx ? videoCtx->audioBufferSize : -1);

        SDL_memset(stream, 0, len);

        if (!videoCtx || !videoCtx->audioDevice || videoCtx->audioBufferAllocatedSize == 0 || !videoCtx->audioBuffer) {
            logError("FFmpeg: audioCallback returning early: context/device/buffer invalid. videoCtx: %p, audioDevice: %u, allocatedSize: %u, audioBuffer: %p",
                (void*)videoCtx,
                videoCtx ? videoCtx->audioDevice : 0,
                videoCtx ? videoCtx->audioBufferAllocatedSize : 0,
                videoCtx ? (void*)videoCtx->audioBuffer : (void*)nullptr);
            return;
        }

        uint32_t amountToCopy;
        int currentStreamPos = 0;

        while (currentStreamPos < len) {
            if (videoCtx->audioBufferPos >= videoCtx->audioBufferSize) {
                logError("FFmpeg: audioCallback needs more audio data (bufferPos %u >= bufferSize %u). Calling decodeNextAudioPacket.",
                    videoCtx->audioBufferPos, videoCtx->audioBufferSize);

                videoCtx->audioBufferPos = 0;
                videoCtx->audioBufferSize = 0;

                if (!decodeNextAudioPacket(*videoCtx)) {
                    logError("FFmpeg: audioCallback: decodeNextAudioPacket failed or returned EOF/no data. No more audio data to provide for this request. Stream pos: %d, requested len: %d", currentStreamPos, len);
                    return;
                }
                if (videoCtx->audioBufferSize == 0) {
                    logError("FFmpeg: audioCallback: decodeNextAudioPacket succeeded but returned 0 bufferSize. No audio data. Stream pos: %d, requested len: %d", currentStreamPos, len);
                    return;
                }
                logError("FFmpeg: audioCallback: decodeNextAudioPacket provided new data. New bufferSize: %u, audioBufferPos is %u.", videoCtx->audioBufferSize, videoCtx->audioBufferPos);
            }

            amountToCopy = std::min(static_cast<uint32_t>(len - currentStreamPos), videoCtx->audioBufferSize - videoCtx->audioBufferPos);

            if (amountToCopy == 0 && (videoCtx->audioBufferSize - videoCtx->audioBufferPos > 0)) {
                logError("FFmpeg error: audioCallback: amountToCopy is 0 but data available in buffer (%u). This implies len - currentStreamPos is 0. Breaking.", videoCtx->audioBufferSize - videoCtx->audioBufferPos);
                break;
            }
            if (amountToCopy == 0 && (videoCtx->audioBufferSize - videoCtx->audioBufferPos == 0)) {
                logError("FFmpeg error: audioCallback: amountToCopy is 0 and internal buffer is also consumed. Will try to decode next audio packet.");
                if (videoCtx->audioBufferPos >= videoCtx->audioBufferSize) continue;
                else break;
            }

            logError("FFmpeg: audioCallback: Before SDL_memcpy - amountToCopy: %u, audioBufferPos: %u, audioBufferSize: %u, debugPos: %d",
                amountToCopy, videoCtx->audioBufferPos, videoCtx->audioBufferSize, currentStreamPos);

            if (videoCtx->audioBuffer + videoCtx->audioBufferPos == nullptr) {
                logError("FFmpeg error: Failed to critical FFmpeg error: videoCtx->audioBuffer + videoCtx->bufferPos is NULL before memcpy!");
                return;
            }
            if (stream + currentStreamPos == nullptr) {
                logError("FFmpeg error: Failed to FFmpeg error: critical: stream + currentStreamPos is NULL before memcpy!");
                return;
            }

            SDL_memcpy(stream + currentStreamPos, videoCtx->audioBuffer + videoCtx->audioBufferPos, amountToCopy);

            videoCtx->audioBufferPos += amountToCopy;
            currentStreamPos += amountToCopy;
            logError("FFmpeg: audioCallback: After SDL_memcpy - new audioBufferPos: %u, currentStreamPos: %d, requested len: %d, remaining in stream: %d",
                videoCtx->audioBufferPos, currentStreamPos, len, len - currentStreamPos);
        }

        logError("FFmpeg: audioCallback exiting. Filled audio stream successfully. Total copied: %d", currentStreamPos);
    }