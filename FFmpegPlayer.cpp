#include "FFmpegPlayer.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QByteArray>
#include <QMutexLocker>
#include <QDebug>

#include <iostream>
#include <cstring>


// 音频 PCM 缓冲设备
// QAudioSink 从这里读取 PCM
// FFmpeg 解码线程向这里写入 PCM
class AudioBufferDevice : public QIODevice{
public:
    explicit AudioBufferDevice(QObject* parent = nullptr): QIODevice(parent), paused(true), maxBufferSize(1024 * 1024){
    }

    void setPaused(bool value){
        QMutexLocker locker(&mutex);

        paused = value;

        if(paused){
            buffer.clear();
        }
    }

    void clearBuffer(){
        QMutexLocker locker(&mutex);
        buffer.clear();
    }

    void appendData(const QByteArray& data){
        if(data.isEmpty()) return;

        QMutexLocker locker(&mutex);

        if(paused) return;

        // 限制最大缓冲区，防止解码速度过快导致内存无限增长
        if(buffer.size() + data.size() > maxBufferSize){
            int removeSize = buffer.size() + data.size() - maxBufferSize;

            if(removeSize > 0 && removeSize < buffer.size()){
                buffer.remove(0, removeSize);
            }
            else if(removeSize >= buffer.size()){
                buffer.clear();
            }
        }

        buffer.append(data);
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override{
        if(maxlen <= 0) return 0;

        QMutexLocker locker(&mutex);

        // 暂停时输出静音
        if(paused || buffer.isEmpty()){
            std::memset(data, 0, static_cast<size_t>(maxlen));
            return maxlen;
        }

        qint64 readSize = qMin(maxlen, static_cast<qint64>(buffer.size()));

        std::memcpy(data, buffer.constData(), static_cast<size_t>(readSize));

        buffer.remove(0, static_cast<int>(readSize));

        // 数据不足时补静音，避免 QAudioSink 进入 Idle
        if(readSize < maxlen){
            std::memset(data + readSize, 0, static_cast<size_t>(maxlen - readSize));
        }

        return maxlen;
    }

    qint64 writeData(const char*, qint64) override{
        return -1;
    }

    qint64 bytesAvailable() const override{
        QMutexLocker locker(&mutex);

        // 即使暂时没有 PCM，也让 QAudioSink 保持工作
        return buffer.size() + 4096 + QIODevice::bytesAvailable();
    }

private:
    mutable QMutex mutex;

    QByteArray buffer;

    bool paused;

    const int maxBufferSize;
};


FFmpegPlayer::FFmpegPlayer(): formatContext(nullptr),
    videoCodecContext(nullptr), videoCodec(nullptr), videoStreamIndex(-1),
    audioCodecContext(nullptr), audioCodec(nullptr), audioStreamIndex(-1),
    swsContext(nullptr), swrContext(nullptr),
    packet(nullptr), videoFrame(nullptr), audioFrame(nullptr),
    opened(false), playing(false), endOfFile(false),
    decodedVideoPts(0.0), currentVideoTime(0.0),
    audioSink(nullptr), audioDevice(nullptr){
}


FFmpegPlayer::~FFmpegPlayer(){
    close();
}


// ============================================================
// 打开视频文件
// ============================================================
bool FFmpegPlayer::open(const char* filename){
    close();

    // ===== 打开文件 =====
    if(avformat_open_input(&formatContext, filename, nullptr, nullptr) < 0){
        std::cerr << "Failed to open video file" << std::endl;
        return false;
    }

    // ===== 获取流信息 =====
    if(avformat_find_stream_info(formatContext, nullptr) < 0){
        std::cerr << "Failed to find stream info" << std::endl;

        close();

        return false;
    }

    // ===== 查找视频流 =====
    videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if(videoStreamIndex < 0){
        std::cerr << "No video stream found" << std::endl;

        close();

        return false;
    }

    // ===== 初始化视频解码器 =====
    AVStream* videoStream = formatContext->streams[videoStreamIndex];

    videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if(!videoCodec){
        std::cerr << "Video decoder not found" << std::endl;

        close();

        return false;
    }


    videoCodecContext = avcodec_alloc_context3(videoCodec);
    if(!videoCodecContext){
        std::cerr << "Failed to allocate video codec context" << std::endl;

        close();

        return false;
    }


    if(avcodec_parameters_to_context(videoCodecContext, videoStream->codecpar) < 0){
        std::cerr << "Failed to copy video codec parameters" << std::endl;

        close();

        return false;
    }


    if(avcodec_open2(videoCodecContext, videoCodec, nullptr) < 0){
        std::cerr << "Failed to open video decoder" << std::endl;

        close();

        return false;
    }


    std::cout << "Video opened successfully" << std::endl;
    std::cout << "Width: " << videoCodecContext->width << std::endl;
    std::cout << "Height: " << videoCodecContext->height << std::endl;
    std::cout << "Video stream index: " << videoStreamIndex << std::endl;


    // ========================================================
    // 音频
    // ========================================================
    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if(audioStreamIndex >= 0){
        AVStream* audioStream = formatContext->streams[audioStreamIndex];

        audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if(audioCodec){
            audioCodecContext = avcodec_alloc_context3(audioCodec);
            if(audioCodecContext){
                if(avcodec_parameters_to_context(audioCodecContext, audioStream->codecpar) == 0){
                    if(avcodec_open2(audioCodecContext, audioCodec, nullptr) == 0){
                        std::cout << "Audio decoder opened successfully" << std::endl;

                        std::cout << "Sample rate: " << audioCodecContext->sample_rate << std::endl;

                        std::cout << "Channels: " << audioCodecContext->ch_layout.nb_channels << std::endl;
                    }
                }
            }
        }


        // ===== 创建音频重采样器 =====
        if(audioCodecContext){
            int ret = swr_alloc_set_opts2(&swrContext, &audioCodecContext->ch_layout, AV_SAMPLE_FMT_S16, audioCodecContext->sample_rate, &audioCodecContext->ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate, 0,nullptr);
            if(ret < 0 || !swrContext){
                std::cerr << "Failed to create audio resampler" << std::endl;

                swr_free(&swrContext);
            }
            else if(swr_init(swrContext) < 0){
                std::cerr << "Failed to initialize audio resampler" << std::endl;

                swr_free(&swrContext);
            }
        }
    }

    // FFmpeg对象
    // ===== 分配Packet和Frame =====
    packet = av_packet_alloc();
    videoFrame = av_frame_alloc();
    audioFrame = av_frame_alloc();


    if(!packet || !videoFrame || !audioFrame){
        std::cerr << "Failed to allocate FFmpeg packet/frame" << std::endl;

        close();

        return false;
    }


    {
        QMutexLocker locker(&stateMutex);

        opened = true;
        playing = false;
        endOfFile = false;

        decodedVideoPts = 0.0;
        currentVideoTime = 0.0;
        //audioClock = 0.0;
    }


    return true;
}


// ============================================================
// 关闭
// ============================================================
void FFmpegPlayer::close(){
    {
        QMutexLocker locker(&stateMutex);

        opened = false;
        playing = false;
        endOfFile = true;
    }


    // ===== 音频 =====

    if(audioSink){
        audioSink->stop();

        delete audioSink;

        audioSink = nullptr;
    }

    if(audioDevice){
        audioDevice->close();
        delete audioDevice;
        audioDevice = nullptr;
    }

    // ===== Packet / Frame =====
    if(packet){
        av_packet_free(&packet);
    }


    if(videoFrame){
        av_frame_free(&videoFrame);
    }


    if(audioFrame){
        av_frame_free(&audioFrame);
    }


    // ===== 转换器 =====

    if(swsContext){
        sws_freeContext(swsContext);

        swsContext = nullptr;
    }


    if(swrContext){
        swr_free(&swrContext);
    }


    // ===== 解码器 =====

    if(videoCodecContext){
        avcodec_free_context(&videoCodecContext);
    }


    if(audioCodecContext){
        avcodec_free_context(&audioCodecContext);
    }


    // ===== 文件 =====

    if(formatContext){
        avformat_close_input(&formatContext);
    }


    formatContext = nullptr;

    videoCodec = nullptr;
    audioCodec = nullptr;

    videoStreamIndex = -1;
    audioStreamIndex = -1;

    decodedVideoPts = 0.0;
    currentVideoTime = 0.0;
    //audioClock = 0.0;
}


// ============================================================
// 初始化音频输出
// 注意：由解码线程调用
// ============================================================
bool FFmpegPlayer::initAudioOutput(){
    if(!opened) return false;

    if(audioStreamIndex < 0 || !audioCodecContext) return true;

    // 已经初始化
    if(audioSink) return true;


    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!device.isNull()){
        audioFormat = device.preferredFormat();
    }

    // 使用 Int16 作为 FFmpeg → Qt 的统一 PCM 格式
    audioFormat.setSampleFormat(QAudioFormat::Int16);
    audioFormat.setSampleRate(audioCodecContext->sample_rate);
    audioFormat.setChannelCount(audioCodecContext->ch_layout.nb_channels);

    // 如果当前设备不支持这个格式
    // 使用设备默认格式的采样率和声道数
    if (!device.isNull() && !device.isFormatSupported(audioFormat)){
        QAudioFormat fallback = device.preferredFormat();

        fallback.setSampleFormat(QAudioFormat::Int16);

        audioFormat = fallback;
    }


    // 创建线程安全 PCM 缓冲
    audioDevice = new AudioBufferDevice();
    audioDevice->open(QIODevice::ReadOnly);
    audioDevice->setPaused(true);


    // 创建 QAudioSink
    audioSink = new QAudioSink(device, audioFormat);
    audioSink->setBufferSize(audioFormat.sampleRate() * audioFormat.channelCount() * 2 / 4);

    // QAudioSink 在 GUI 线程工作
    audioSink->start(audioDevice);


    // 创建 FFmpeg 重采样器
    // FFmpeg内部格式 → S16 PCM
    if(!swrContext){
        AVChannelLayout outputLayout;

        av_channel_layout_default(&outputLayout, audioFormat.channelCount());

        int ret = swr_alloc_set_opts2(&swrContext, &outputLayout, AV_SAMPLE_FMT_S16, audioFormat.sampleRate(), &audioCodecContext->ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate, 0, nullptr);

        av_channel_layout_uninit(&outputLayout);

        if(ret < 0 || !swrContext){
            std::cerr << "Failed to create audio resampler." << std::endl;

            return false;
        }

        if(swr_init(swrContext) < 0){
            std::cerr << "Failed to initialize audio resampler." << std::endl;

            return false;
        }
    }

    std::cout << "Audio output initialized." << std::endl;

    return true;
}


// ============================================================
// 播放
// ============================================================
void FFmpegPlayer::play(){
    QMutexLocker locker(&stateMutex);

    playing = true;

    if(audioDevice){
        audioDevice->setPaused(false);
    }
}


// ============================================================
// 暂停
// ============================================================
void FFmpegPlayer::pause(){
    QMutexLocker locker(&stateMutex);

    playing = false;

    if(audioDevice){
        audioDevice->setPaused(true);
    }
}


// ============================================================
// 停止
// ============================================================
void FFmpegPlayer::stop(){
    QMutexLocker locker(&stateMutex);

    playing = false;

    if(audioDevice){
        audioDevice->setPaused(true);
        audioDevice->clearBuffer();
    }

    flushDecoders();

    currentVideoTime = 0.0;
    decodedVideoPts = 0.0;
}


// ============================================================
// 视频解码
// ============================================================
bool FFmpegPlayer::decodeNextVideoFrame(QImage& image){
    if(!formatContext || !videoCodecContext || !packet || !videoFrame){
        return false;
    }


    // ===== 确保音频输出已经建立 =====
    if(audioCodecContext && !audioSink){
        initAudioOutput();
    }


    while(true){
        int ret = av_read_frame(formatContext, packet);

        // ===== EOF =====
        if(ret < 0){
            endOfFile = true;

            return false;
        }

        // ====================================================
        // 音频Packet
        // ====================================================
        if(packet->stream_index == audioStreamIndex){
            decodeAudioPacket(packet);

            av_packet_unref(packet);

            continue;
        }


        // ====================================================
        // 视频Packet
        // ====================================================
        if(packet->stream_index == videoStreamIndex){
            ret = avcodec_send_packet(videoCodecContext, packet);

            av_packet_unref(packet);

            if(ret < 0){
                continue;
            }


            while(true){
                ret = avcodec_receive_frame(videoCodecContext, videoFrame);
                if(ret == AVERROR(EAGAIN)){
                    break;
                }
                if(ret == AVERROR_EOF){
                    return false;
                }
                if(ret < 0){
                    break;
                }


                // ===== 得到视频PTS =====
                int64_t pts = videoFrame->best_effort_timestamp;
                if(pts == AV_NOPTS_VALUE){
                    pts = videoFrame->pts;
                }
                if(pts != AV_NOPTS_VALUE){
                    AVStream* stream = formatContext->streams[videoStreamIndex];

                    decodedVideoPts = pts * av_q2d(stream->time_base);

                    currentVideoTime = decodedVideoPts;
                }


                // ===== YUV -> RGB =====
                if(!convertVideoFrame(videoFrame, image)){
                    return false;
                }

                return true;
            }

            continue;
        }

        // ===== 其它流 =====
        av_packet_unref(packet);
    }
}


// ============================================================
// 视频格式转换
// ============================================================
bool FFmpegPlayer::convertVideoFrame(AVFrame* frame, QImage& image){
    if(!frame){
        return false;
    }

    if(!swsContext){
        swsContext = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    if(!swsContext){
        return false;
    }

    QImage result(frame->width, frame->height, QImage::Format_RGB32);

    uint8_t* dstData[4] = {result.bits(), nullptr, nullptr, nullptr};

    int dstLinesize[4] ={ static_cast<int>(result.bytesPerLine()), 0, 0, 0};

    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);

    image = result.copy();

    return true;
}


// ============================================================
// 音频Packet解码
// ============================================================
bool FFmpegPlayer::decodeAudioPacket(AVPacket* packet){
    if(!audioCodecContext || !audioFrame || !swrContext || !packet){
        return false;
    }


    int ret = avcodec_send_packet(audioCodecContext, packet);
    if(ret < 0){
        return false;
    }

    bool decoded = false;

    while(true){
        ret = avcodec_receive_frame(audioCodecContext, audioFrame);
        if(ret == AVERROR(EAGAIN)){
            break;
        }
        if(ret == AVERROR_EOF){
            break;
        }
        if(ret < 0){
            break;
        }


        if(writeAudioFrame(audioFrame)){
            decoded = true;
        }
    }
    return decoded;
}


// ============================================================
// 写入音频 （音频帧 → PCM）
// ============================================================
bool FFmpegPlayer::writeAudioFrame(AVFrame* frame){
    if (!swrContext || !audioDevice){
        return true;
    }

    int inputRate = audioCodecContext->sample_rate;
    int outputRate = audioFormat.sampleRate();
    int outputChannels = audioFormat.channelCount();


    int64_t delay = swr_get_delay(swrContext, inputRate);

    int outputSamples = static_cast<int>(av_rescale_rnd(delay + frame->nb_samples, outputRate, inputRate, AV_ROUND_UP));
    if(outputSamples <= 0) return true;


    int bytesPerSample = 2;

    int bufferSize = outputSamples * outputChannels * bytesPerSample;


    QByteArray pcm;
    pcm.resize(bufferSize);


    uint8_t* outputData[1];
    outputData[0] = reinterpret_cast<uint8_t*>(pcm.data());

    int converted = swr_convert(swrContext, outputData, outputSamples, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    if(converted <= 0) return true;


    int actualSize = converted * outputChannels * bytesPerSample;

    pcm.resize(actualSize);


    // 送入线程安全 PCM 缓冲
    audioDevice->appendData(pcm);

    return true;
}


// ============================================================
// Seek
// 注意：这个函数现在只能由DecodeThread调用
// ============================================================
bool FFmpegPlayer::seek(double seconds){
    if(!opened || !formatContext){
        return false;
    }


    if(seconds < 0) seconds = 0;

    double duration = getDuration();
    if(duration > 0 && seconds > duration){
        seconds = duration;
    }


    // 清空已经缓存的音频
    if(audioDevice){
        audioDevice->clearBuffer();
    }


    AVStream* stream = formatContext->streams[videoStreamIndex];

    int64_t timestamp = static_cast<int64_t>(seconds / av_q2d(stream->time_base));

    int ret = av_seek_frame(formatContext, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if(ret < 0) return false;

    flushDecoders();

    currentVideoTime = seconds;
    decodedVideoPts = seconds;
    endOfFile = false;

    return true;
}


// ============================================================
// 清空解码器
// ============================================================
void FFmpegPlayer::flushDecoders(){
    if(videoCodecContext){
        avcodec_flush_buffers(videoCodecContext);
    }


    if(audioCodecContext){
        avcodec_flush_buffers(audioCodecContext);
    }


    if(swrContext){
        swr_close(swrContext);

        if(swr_init(swrContext) < 0){
            std::cerr << "Failed to reinitialize audio resampler." << std::endl;
        }
    }


    if(packet){
        av_packet_unref(packet);
    }
}


// ============================================================
// 获取视频PTS
// ============================================================
double FFmpegPlayer::getDecodedVideoPts() const{
    return decodedVideoPts;
}


// ============================================================
// 获取当前播放时间
// ============================================================
double FFmpegPlayer::getCurrentTime() const{
    QMutexLocker locker(&stateMutex);

    return currentVideoTime;
}


// ============================================================
// 获取总时长
// ============================================================
double FFmpegPlayer::getDuration() const{
    if(!formatContext){
        return 0.0;
    }

    if(formatContext->duration == AV_NOPTS_VALUE){
        return 0.0;
    }

    return static_cast<double>(formatContext->duration) / AV_TIME_BASE;
}


// ============================================================
// 获取帧率
// ============================================================
double FFmpegPlayer::getFrameRate() const{
    if(!formatContext || videoStreamIndex < 0){
        return 0.0;
    }

    AVStream* stream = formatContext->streams[videoStreamIndex];

    if(stream->avg_frame_rate.den == 0) return 0.0;

    return av_q2d(stream->avg_frame_rate);
}


// ============================================================
// 状态
// ============================================================
bool FFmpegPlayer::isPlaying() const{
    QMutexLocker locker(&stateMutex);

    return playing;
}


bool FFmpegPlayer::isOpened() const{
    QMutexLocker locker(&stateMutex);

    return opened;
}

// bool FFmpegPlayer::isEndOfFile() const{
//     QMutexLocker locker(&stateMutex);

//     return endOfFile;
// }