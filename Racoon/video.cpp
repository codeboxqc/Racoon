#include "Header.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <algorithm>
#include <string> // For std::to_string in logging

#ifdef min
#undef min
#endif

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h> // Added for av_make_error_string
}

// logError is now declared in Header.h and defined in Racoon.cpp

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

                        videoCtx.decodedAudioFrame = av_frame_alloc();
                        videoCtx.audioPacket = av_packet_alloc();
                        if (!videoCtx.decodedAudioFrame || !videoCtx.audioPacket) {
                            logError("FFmpeg: Failed to allocate audio frame/packet");
                            av_frame_free(&videoCtx.decodedAudioFrame);
                            av_packet_free(&videoCtx.audioPacket);
                            avcodec_free_context(&videoCtx.audioCodecContext);
                        }
                        else {
                            // Setup SwrContext for resampling to S16
                            videoCtx.swrContext = swr_alloc();
                            if (!videoCtx.swrContext) {
                                logError("FFmpeg: WARN Cannot allocate SwrContext. Audio may not play.");
                                av_frame_free(&videoCtx.decodedAudioFrame);
                                av_packet_free(&videoCtx.audioPacket);
                                avcodec_free_context(&videoCtx.audioCodecContext);
                            }
                            else {
                                AVChannelLayout out_ch_layout;
                                av_channel_layout_copy(&out_ch_layout, &videoCtx.audioChannelLayout); // Default to input layout for output if not specified otherwise
                                AVChannelLayout in_ch_layout;
                                av_channel_layout_copy(&in_ch_layout, &videoCtx.audioChannelLayout);

                                // Log SwrContext options
                                logError("FFmpeg: SwrContext options: out_ch_layout channels: %d, out_sample_rate: %d, out_sample_fmt: %s, in_ch_layout channels: %d, in_sample_rate: %d, in_sample_fmt: %s",
                                    out_ch_layout.nb_channels, pAudioCodecParams->sample_rate, av_get_sample_fmt_name(AV_SAMPLE_FMT_S16),
                                    in_ch_layout.nb_channels, pAudioCodecParams->sample_rate, av_get_sample_fmt_name(static_cast<AVSampleFormat>(pAudioCodecParams->format)));

                                // Set options for SwrContext
                                av_opt_set_chlayout(videoCtx.swrContext, "out_channel_layout", &out_ch_layout, 0);
                                av_opt_set_int(videoCtx.swrContext, "out_sample_rate", pAudioCodecParams->sample_rate, 0);
                                av_opt_set_sample_fmt(videoCtx.swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                                av_opt_set_chlayout(videoCtx.swrContext, "in_channel_layout", &in_ch_layout, 0);
                                av_opt_set_int(videoCtx.swrContext, "in_sample_rate", pAudioCodecParams->sample_rate, 0);
                                av_opt_set_sample_fmt(videoCtx.swrContext, "in_sample_fmt", static_cast<AVSampleFormat>(pAudioCodecParams->format), 0);

                                int swr_init_ret = swr_init(videoCtx.swrContext);
                                if (swr_init_ret < 0) {
                                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                                    av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, swr_init_ret);
                                    logError("FFmpeg: WARN Cannot initialize SwrContext. Error: %s. Audio may not play.", errBuf);
                                    swr_free(&videoCtx.swrContext);
                                    av_frame_free(&videoCtx.decodedAudioFrame);
                                    av_packet_free(&videoCtx.audioPacket);
                                    avcodec_free_context(&videoCtx.audioCodecContext);
                                }
                                else {
                                    logError("FFmpeg: Audio SwrContext initialized for S16 conversion.");
                                }

                                // Clean up copied layouts
                                av_channel_layout_uninit(&out_ch_layout);
                                av_channel_layout_uninit(&in_ch_layout);
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

    // 6. Initialize SwsContext for video frame conversion
    if (videoCtx.videoCodecContext) {
        videoCtx.swsContext = sws_getContext(
            videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height, videoCtx.videoCodecContext->pix_fmt,
            videoCtx.videoCodecContext->width, videoCtx.videoCodecContext->height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
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
        // Try to receive a frame first, in case decoder has buffered data
        ret = avcodec_receive_frame(videoCtx.audioCodecContext, videoCtx.decodedAudioFrame);
        if (ret == 0) { // Successfully got a frame
            logError("FFmpeg: Successfully received audio frame (prior to reading new packet).");
            // Process this frame
        }
        else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // Need more data or end of stream from decoder, so read a new packet
            if (ret == AVERROR_EOF) {
                logError("FFmpeg: avcodec_receive_frame returned AVERROR_EOF before reading new packet. Decoder fully flushed.");
                // No need to unref packet here as it's not yet read or was processed.
                return false; // Decoder is flushed and no more data.
            }

            logError("FFmpeg: avcodec_receive_frame returned EAGAIN. Attempting to read new packet.");
            av_packet_unref(packet); // Ensure packet is clean before reading
            ret = av_read_frame(videoCtx.formatContext, packet);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    logError("FFmpeg: av_read_frame reached EOF (audio). Sending NULL packet to flush decoder.");
                    // Send a NULL packet to the decoder to flush remaining frames
                    ret = avcodec_send_packet(videoCtx.audioCodecContext, nullptr);
                    if (ret < 0) {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE];
                        av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                        logError("FFmpeg: Error sending NULL flush packet to audio decoder: %s (code %d)", errBuf, ret);
                        return false; // Cannot flush.
                    }
                    // Now try to receive the flushed frames in the next iteration's avcodec_receive_frame
                    continue;
                }
                else {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                    logError("FFmpeg: av_read_frame error (audio): %s (code %d)", errBuf, ret);
                    return false; // Error reading frame
                }
            }

            if (packet->stream_index != videoCtx.audioStreamIndex) {
                av_packet_unref(packet); // Not for us
                continue;
            }

            logError("FFmpeg: Processing an audio packet from stream %d. Size: %d, PTS: %lld", packet->stream_index, packet->size, packet->pts);
            ret = avcodec_send_packet(videoCtx.audioCodecContext, packet);
            if (ret < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                logError("FFmpeg: Error sending audio packet to decoder: %s (code %d)", errBuf, ret);
                av_packet_unref(packet);
                if (ret == AVERROR(EAGAIN)) { // Should not happen if we just received EAGAIN from receive_frame
                    logError("FFmpeg: avcodec_send_packet returned EAGAIN unexpectedly, continuing.");
                    continue;
                }
                return false; // Other error
            }
            // Packet is now owned by decoder, try to receive frame again in next loop iteration
            av_packet_unref(packet); // Decoder has it or copied it.
            continue; // Go back to avcodec_receive_frame

        }
        else { // Other avcodec_receive_frame error
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
            logError("FFmpeg: Error receiving audio frame from decoder: %s (code %d)", errBuf, ret);
            // No packet to unref here as it's about receiving.
            return false;
        }

        // Frame processing logic (moved from original position)
        uint8_t* out_buffer_ptr[1] = { videoCtx.audioBuffer }; // swr_convert expects array of pointers
        int out_samples = 0;

        if (videoCtx.swrContext) {
            logError("FFmpeg: swr_convert input: nb_samples=%d, sample_rate=%d, format=%s, ch_layout_nb_channels=%d",
                videoCtx.decodedAudioFrame->nb_samples, videoCtx.decodedAudioFrame->sample_rate,
                av_get_sample_fmt_name(static_cast<AVSampleFormat>(videoCtx.decodedAudioFrame->format)),
                videoCtx.decodedAudioFrame->ch_layout.nb_channels);

            int max_out_samples = av_rescale_rnd(videoCtx.decodedAudioFrame->nb_samples, videoCtx.obtainedAudioSpec.freq, videoCtx.decodedAudioFrame->sample_rate, AV_ROUND_UP);
            uint32_t required_buffer_size = static_cast<uint32_t>(max_out_samples * videoCtx.obtainedAudioSpec.channels * 2); // 2 for S16

            if (videoCtx.audioBufferAllocatedSize < required_buffer_size) {
                logError("FFmpeg: audioBuffer too small for resampled data. Need %u, have %u. This is a critical error.",
                    required_buffer_size, videoCtx.audioBufferAllocatedSize);
                // TODO: Consider reallocating audioBuffer here if this happens, though ideally initial allocation is sufficient.
                // For now, just log and potentially fail or truncate.
                // Returning false here might be too abrupt. Let's see if swr_convert handles smaller output buffer.
                // It's better to ensure buffer is large enough initially.
                // For this exercise, we assume initial allocation is now robust.
            }

            out_samples = swr_convert(videoCtx.swrContext, out_buffer_ptr, max_out_samples,
                (const uint8_t**)videoCtx.decodedAudioFrame->extended_data, videoCtx.decodedAudioFrame->nb_samples);

            if (out_samples < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, out_samples);
                logError("FFmpeg: swr_convert error: %s (code %d)", errBuf, out_samples);
                return false; // Error during conversion
            }
            videoCtx.audioBufferSize = out_samples * videoCtx.obtainedAudioSpec.channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            logError("FFmpeg: swr_convert output: %d samples, calculated audioBufferSize: %u", out_samples, videoCtx.audioBufferSize);
        }
        else if (videoCtx.audioCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 &&
            videoCtx.audioCodecContext->ch_layout.nb_channels == videoCtx.obtainedAudioSpec.channels &&
            videoCtx.audioCodecContext->sample_rate == videoCtx.obtainedAudioSpec.freq) {
            // Direct copy if format, channels, and sample rate match
            int data_size = av_samples_get_buffer_size(nullptr, videoCtx.audioCodecContext->ch_layout.nb_channels,
                videoCtx.decodedAudioFrame->nb_samples, videoCtx.audioCodecContext->sample_fmt, 1); // Align to 1 byte
            logError("FFmpeg: Using direct memcpy for S16 audio (format, channels, rate match). Calculated data_size: %d. Frame samples: %d, Frame channels: %d, Frame format: %s",
                data_size, videoCtx.decodedAudioFrame->nb_samples, videoCtx.audioCodecContext->ch_layout.nb_channels, av_get_sample_fmt_name(videoCtx.audioCodecContext->sample_fmt));

            if (data_size > 0 && static_cast<uint32_t>(data_size) <= videoCtx.audioBufferAllocatedSize) {
                memcpy(videoCtx.audioBuffer, videoCtx.decodedAudioFrame->data[0], data_size);
                videoCtx.audioBufferSize = data_size;
            }
            else if (data_size > 0) { // data_size > allocated size
                logError("FFmpeg: audioBuffer too small for decoded data (S16 direct, matching spec). Need %d, have %u. Truncating.", data_size, videoCtx.audioBufferAllocatedSize);
                memcpy(videoCtx.audioBuffer, videoCtx.decodedAudioFrame->data[0], videoCtx.audioBufferAllocatedSize);
                videoCtx.audioBufferSize = videoCtx.audioBufferAllocatedSize;
            }
            else {
                logError("FFmpeg: Calculated data_size for direct S16 copy is %d. Setting audioBufferSize to 0.", data_size);
                videoCtx.audioBufferSize = 0;
            }
        }
        else {
            logError("FFmpeg: CRITICAL WARNING: Audio format mismatch and no swrContext, or S16 spec mismatch. CANNOT PLAY AUDIO. Decoded Format: %s, Rate: %d, Channels: %d. Obtained Spec: Format AUDIO_S16SYS, Rate: %d, Channels: %d",
                av_get_sample_fmt_name(static_cast<AVSampleFormat>(videoCtx.decodedAudioFrame->format)), videoCtx.decodedAudioFrame->sample_rate, videoCtx.decodedAudioFrame->ch_layout.nb_channels,
                videoCtx.obtainedAudioSpec.freq, videoCtx.obtainedAudioSpec.channels);
            videoCtx.audioBufferSize = 0; // No data to play
        }

        videoCtx.audioBufferPos = 0;
        av_frame_unref(videoCtx.decodedAudioFrame); // Unref the frame after processing
        logError("FFmpeg: decodeNextAudioPacket returning true (successfully decoded and processed a frame). audioBufferSize: %u", videoCtx.audioBufferSize);
        return true; // Successfully processed a frame
    }
    // Should not be reached if loop logic is correct (either returns true with data, false on EOF/error)
    logError("FFmpeg: decodeNextAudioPacket returning false (exited loop unexpectedly).");
    return false;
}


extern "C" void audioCallback(void* userdata, uint8_t* stream, int len) {
    VideoContext* videoCtx = static_cast<VideoContext*>(userdata);
    logError("FFmpeg: audioCallback entered. Requested len: %d. Current audioBufferPos: %u, audioBufferSize: %u",
        len, videoCtx ? videoCtx->audioBufferPos : -1, videoCtx ? videoCtx->audioBufferSize : -1);

    SDL_memset(stream, 0, len); // Clear the stream buffer first

    if (!videoCtx || !videoCtx->audioDevice || videoCtx->audioBufferAllocatedSize == 0 || !videoCtx->audioBuffer) {
        logError("FFmpeg: audioCallback returning early: context/device/buffer invalid. videoCtx: %p, audioDevice: %u, allocatedSize: %u, audioBuffer: %p",
            (void*)videoCtx, videoCtx ? videoCtx->audioDevice : 0, videoCtx ? videoCtx->audioBufferAllocatedSize : 0, videoCtx ? (void*)videoCtx->audioBuffer : (void*)nullptr);
        return;
    }

    uint32_t amountToCopy;
    int currentStreamPos = 0; // How much we've written to `stream` (use int to match len)

    while (currentStreamPos < len) { // While there's still space in `stream` to fill
        if (videoCtx->audioBufferPos >= videoCtx->audioBufferSize) {
            logError("FFmpeg: audioCallback needs more data (bufferPos %u >= bufferSize %u). Calling decodeNextAudioPacket.",
                videoCtx->audioBufferPos, videoCtx->audioBufferSize);

            // Reset buffer position and size before decoding new packet
            videoCtx->audioBufferPos = 0;
            videoCtx->audioBufferSize = 0;

            if (!decodeNextAudioPacket(*videoCtx)) {
                logError("FFmpeg: audioCallback: decodeNextAudioPacket failed or returned EOF/no data. No more audio data to provide for this request. Stream pos: %d, requested len: %d", currentStreamPos, len);
                // No more data to provide, the rest of `stream` will remain silent (already memset to 0)
                return;
            }
            if (videoCtx->audioBufferSize == 0) {
                logError("FFmpeg: audioCallback: decodeNextAudioPacket succeeded but returned 0 bufferSize. No audio data. Stream pos: %d, requested len: %d", currentStreamPos, len);
                return; // No data decoded
            }
            logError("FFmpeg: audioCallback: decodeNextAudioPacket provided new data. New bufferSize: %u. audioBufferPos is %u.", videoCtx->audioBufferSize, videoCtx->audioBufferPos);
        }

        amountToCopy = std::min(static_cast<uint32_t>(len - currentStreamPos), videoCtx->audioBufferSize - videoCtx->audioBufferPos);

        if (amountToCopy == 0 && (videoCtx->audioBufferSize - videoCtx->audioBufferPos > 0)) {
            logError("FFmpeg: audioCallback: amountToCopy is 0, but data available in buffer (%u). This implies len - currentStreamPos is 0. Breaking.", videoCtx->audioBufferSize - videoCtx->audioBufferPos);
            break;
        }
        if (amountToCopy == 0 && (videoCtx->audioBufferSize - videoCtx->audioBufferPos == 0)) {
            logError("FFmpeg: audioCallback: amountToCopy is 0 and internal buffer is also consumed. Will try to decode next packet.");
            // This state should be caught by the (videoCtx->audioBufferPos >= videoCtx->audioBufferSize) check at the loop start.
            // If we are here, it means the previous decodeNextAudioPacket might have returned 0 useful bytes.
            // Forcing a re-evaluation or exiting:
            if (videoCtx->audioBufferPos >= videoCtx->audioBufferSize) continue; // Try to decode again.
            else break; // Should not happen.
        }


        logError("FFmpeg: audioCallback: Before SDL_memcpy - amountToCopy: %u, audioBufferPos: %u, audioBufferSize: %u, currentStreamPos: %d",
            amountToCopy, videoCtx->audioBufferPos, videoCtx->audioBufferSize, currentStreamPos);

        if (videoCtx->audioBuffer + videoCtx->audioBufferPos == nullptr) {
            logError("FFmpeg: audioCallback: CRITICAL: videoCtx->audioBuffer + videoCtx->audioBufferPos is NULL before memcpy!");
            return;
        }
        if (stream + currentStreamPos == nullptr) {
            logError("FFmpeg: audioCallback: CRITICAL: stream + currentStreamPos is NULL before memcpy!");
            return;
        }


        SDL_memcpy(stream + currentStreamPos, videoCtx->audioBuffer + videoCtx->audioBufferPos, amountToCopy);

        videoCtx->audioBufferPos += amountToCopy;
        currentStreamPos += amountToCopy;

        logError("FFmpeg: audioCallback: After SDL_memcpy - new audioBufferPos: %u, currentStreamPos: %u, remaining in stream: %d",
            videoCtx->audioBufferPos, currentStreamPos, len - currentStreamPos);
    }
    logError("FFmpeg: audioCallback exiting. Filled stream. Total copied: %d", currentStreamPos);
}

bool decodeVideoFrame(VideoContext& videoCtx, SDL_Texture** videoTexture, SDL_Renderer* renderer) {
    if (!videoCtx.formatContext || !videoCtx.videoCodecContext || !videoCtx.swsContext) {
        return false;
    }

    AVPacket packet;
    packet.data = nullptr;
    packet.size = 0;

    while (av_read_frame(videoCtx.formatContext, &packet) >= 0) {
        if (packet.stream_index == videoCtx.videoStreamIndex) {
            int ret = avcodec_send_packet(videoCtx.videoCodecContext, &packet);
            if (ret < 0) {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                logError("FFmpeg: Error sending video packet to decoder: %s", errBuf);
                av_packet_unref(&packet);
                return false;
            }

            ret = avcodec_receive_frame(videoCtx.videoCodecContext, videoCtx.decodedFrame);
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
                            av_packet_unref(&packet);
                            return false;
                        }
                    }

                    if (SDL_UpdateTexture(*videoTexture, nullptr, videoCtx.rgbBuffer, videoCtx.rgbFrame->linesize[0]) != 0) {
                        logError("FFmpeg: Failed to update SDL_Texture for video: %s", SDL_GetError());
                    }
                }
                av_packet_unref(&packet);
                return true;
            }
            else if (ret == AVERROR(EAGAIN)) {
                av_packet_unref(&packet);
                continue;
            }
            else if (ret == AVERROR_EOF) {
                logError("FFmpeg: Video decoder flushed, EOF reached.");
                av_packet_unref(&packet);
                return false;
            }
            else {
                char errBuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errBuf, AV_ERROR_MAX_STRING_SIZE, ret);
                logError("FFmpeg: Error receiving video frame from decoder: %s", errBuf);
                av_packet_unref(&packet);
                return false;
            }
        }
        av_packet_unref(&packet);
    }

    return false;
}