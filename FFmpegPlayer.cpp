#include "FFmpegPlayer.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QByteArray>
#include <QMutexLocker>
#include <QDebug>

#include <iostream>
#include <cstring>


// ============================================================
// 音频 PCM 缓冲
// QAudioSink（拉模式）从这里读取，解码线程往这里写
//
// 关键约定：
//   1) readData() 永远返回 maxlen，不足部分补静音 —— 绝不返回 0。
//      返回 0 会让 QAudioSink 判定"没有数据"并转入 IdleState 后不再拉取。
//   2) 溢出时按"整帧"裁剪，绝不按任意字节裁剪，否则破坏 S16 样本对齐。
//   3) 暂停/停止不在这里处理，交给 QAudioSink::suspend()/resume()。
//       这样可以保证 processedUSecs() 在暂停期间不增长。
// ============================================================
class AudioBufferDevice : public QIODevice{
public:
    AudioBufferDevice(int channels, int sampleRate, QObject* parent = nullptr): QIODevice(parent),
        m_channels(qMax(1, channels)),
        m_sampleRate(qMax(1, sampleRate)),
        m_frameBytes(m_channels * 2),                                  // S16 交错
        m_maxBytes(qMax(m_frameBytes * 512,   m_sampleRate * m_channels * 2))                // 约 1 秒
    {}

    void openDevice(){
        open(QIODevice::ReadOnly);
    }

    // ---- 解码线程调用 ----
    void appendData(const QByteArray& pcm){
        if(pcm.isEmpty()) return;
        {
            QMutexLocker locker(&m_mutex);
            m_buffer.append(pcm);
            if (m_buffer.size() > m_maxBytes){
                int excess = m_buffer.size() - m_maxBytes;
                // 向上对齐到整帧，保证声道/采样点不错位
                int drop = ((excess + m_frameBytes - 1) / m_frameBytes) * m_frameBytes;
                if(drop >= m_buffer.size()) m_buffer.clear();
                else m_buffer.remove(0, drop);
            }
        }
        emit readyRead();
    }

    void clearBuffer(){
        QMutexLocker locker(&m_mutex); m_buffer.clear();
    }

    qint64 bufferedBytes() const{
        QMutexLocker locker(&m_mutex);
        return m_buffer.size();
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override{
        if (maxlen <= 0) return 0;

        qint64 n = 0;
        {
            QMutexLocker locker(&m_mutex);
            n = qMin(maxlen, static_cast<qint64>(m_buffer.size()));
            if(n > 0){
                std::memcpy(data, m_buffer.constData(), static_cast<size_t>(n));
                m_buffer.remove(0, static_cast<int>(n));
            }
        }

        // 补齐静音并返回完整长度：QAudioSink 永远不会因读到 0 而进入 Idle
        if(n < maxlen) std::memset(data + n, 0, static_cast<size_t>(maxlen - n));
        return maxlen;
    }

    qint64 writeData(const char*, qint64) override{
        return -1;
    }

    qint64 bytesAvailable() const override{
        QMutexLocker locker(&m_mutex);
        return m_buffer.size() + QIODevice::bytesAvailable();
    }

private:
    mutable QMutex m_mutex;
    QByteArray m_buffer;
    int m_channels;
    int m_sampleRate;
    int m_frameBytes;
    int m_maxBytes;
};



FFmpegPlayer::FFmpegPlayer(){

}

FFmpegPlayer::~FFmpegPlayer(){
    close();
}


// ============================================================
// 打开视频文件
// ============================================================
bool FFmpegPlayer::open(const char* filename){
    close();

    if(!filename) return false;

    if(avformat_open_input(&formatContext, filename, nullptr, nullptr) < 0){
        std::cerr << "Failed to open video file: " << filename << std::endl;
        formatContext = nullptr;
        return false;
    }
    if(avformat_find_stream_info(formatContext, nullptr) < 0){
        std::cerr << "Failed to find stream info" << std::endl;
        close();
        return false;
    }

    // ===== 起点偏移：部分文件首 PTS 不是 0 =====
    startTimeOffset = (formatContext->start_time != AV_NOPTS_VALUE) ? formatContext->start_time / static_cast<double>(AV_TIME_BASE): 0.0;

    // ===== 总时长缓存（避免跨线程读 formatContext） =====
    cachedDuration.store((formatContext->duration != AV_NOPTS_VALUE) ? static_cast<double>(formatContext->duration) / AV_TIME_BASE: 0.0);

    // ========================================================
    // 视频
    // ========================================================
    videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if(videoStreamIndex < 0){
        std::cerr << "No video stream found" << std::endl; close();
        return false;
    }

    AVStream* videoStream = formatContext->streams[videoStreamIndex];
    videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if(!videoCodec){
        std::cerr << "Video decoder not found" << std::endl; close();
        return false;
    }

    videoCodecContext = avcodec_alloc_context3(videoCodec);
    if(!videoCodecContext){
        close();
        return false;
    }

    if(avcodec_parameters_to_context(videoCodecContext, videoStream->codecpar) < 0){
        std::cerr << "Failed to copy video codec parameters" << std::endl; close();
        return false;
    }

    videoCodecContext->thread_count = 0;          // 自动多线程解码，明显提速
    if(avcodec_open2(videoCodecContext, videoCodec, nullptr) < 0){
        std::cerr << "Failed to open video decoder" << std::endl;

        close();
        return false;
    }
    std::cout << "Video " << videoCodecContext->width << "x" << videoCodecContext->height << std::endl;

    // ========================================================
    // 音频：这里只打开解码器
    // swr 和 QAudioSink 一律放到 initAudioOutput() 里建，
    // 保证重采样器的输出参数与 audioFormat 永远一致（修 R3）
    // ========================================================
    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if(audioStreamIndex >= 0){
        AVStream* audioStream = formatContext->streams[audioStreamIndex];
        audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);

        bool ok = false;
        if(audioCodec){
            audioCodecContext = avcodec_alloc_context3(audioCodec);
            ok = audioCodecContext && avcodec_parameters_to_context(audioCodecContext, audioStream->codecpar) == 0 && avcodec_open2(audioCodecContext, audioCodec, nullptr) == 0;
        }
        if(!ok){
            std::cerr << "Audio decoder unavailable, fallback to video-only" << std::endl;
            if(audioCodecContext) avcodec_free_context(&audioCodecContext);

            audioCodec = nullptr;
            audioStreamIndex = -1;                 // 当作无音轨处理
        }
        else{
            std::cout << "Audio " << audioCodecContext->sample_rate << "Hz " << audioCodecContext->ch_layout.nb_channels << "ch" << std::endl;
        }
    }

    packet = av_packet_alloc();
    videoFrame = av_frame_alloc();
    audioFrame = av_frame_alloc();
    if(!packet || !videoFrame || !audioFrame){
        close();
        return false;
    }

    {
        QMutexLocker locker(&stateMutex);
        opened = true;
        endOfFile = false;
    }

    playing = false;

    {
        QMutexLocker locker(&clockMutex);
        clockBaseSeconds = 0.0;
        clockRefUsecs    = 0;
        wallAccumSeconds = 0.0;
    }

    wallTimer.invalidate();
    decodedVideoPts.store(0.0);

    return true;
}


// ============================================================
// 关闭
// ============================================================
void FFmpegPlayer::close(){
    {
        QMutexLocker locker(&stateMutex);
        opened = false;
    }

    playing = false;
    endOfFile = true;

    // ===== 音频（GUI 线程调用） =====
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
    if(packet) av_packet_free(&packet);
    if(videoFrame) av_frame_free(&videoFrame);
    if(audioFrame) av_frame_free(&audioFrame);

    // ===== 转换器 =====
    if(swsContext){
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if(swrContext){
        swr_free(&swrContext);
    }
    swsSrcW = swsSrcH = 0;
    swsSrcFmt = -1;

    // ===== 解码器 =====
    if(videoCodecContext) avcodec_free_context(&videoCodecContext);
    if(audioCodecContext) avcodec_free_context(&audioCodecContext);

    // ===== 文件 =====
    if(formatContext) avformat_close_input(&formatContext);
    formatContext = nullptr;

    videoCodec = nullptr;
    audioCodec = nullptr;
    videoStreamIndex = -1;
    audioStreamIndex = -1;

    cachedDuration.store(0.0);
    decodedVideoPts.store(0.0);
    wallTimer.invalidate();
}



// ============================================================
// 初始化音频输出
// 注意：由解码线程调用
// ============================================================
bool FFmpegPlayer::initAudioOutput(){
    if(!opened) return false;
    if(audioStreamIndex < 0 || !audioCodecContext) return true;   // 无音轨，正常
    if(audioSink) return true;                                    // 已初始化

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if(device.isNull()){
        std::cerr << "No audio output device" << std::endl;
        return false;
    }

    // ===== 1. 敲定"最终生效"的播放格式 =====
    QAudioFormat fmt = device.preferredFormat();
    fmt.setSampleFormat(QAudioFormat::Int16);

    // 尽量沿用文件采样率（少一次重采样）；声道数统一压到 2 以内
    QAudioFormat fileFmt = fmt;
    fileFmt.setSampleRate(audioCodecContext->sample_rate);
    fileFmt.setChannelCount(qBound(1, audioCodecContext->ch_layout.nb_channels, 2));
    if(device.isFormatSupported(fileFmt)) fmt = fileFmt;
    else std::cout << "Device rejects file format, using preferred" << std::endl;

    audioFormat = fmt;

    // ===== 2. 以 audioFormat 为输出参数建重采样器（唯一一处建 swr） =====
    if(swrContext){
        swr_free(&swrContext);
    }

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, audioFormat.channelCount());

    int ret = swr_alloc_set_opts2(&swrContext, &outLayout, AV_SAMPLE_FMT_S16, audioFormat.sampleRate(), &audioCodecContext->ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outLayout);
    if(ret < 0 || !swrContext){
        std::cerr << "Failed to alloc audio resampler" << std::endl;
        swrContext = nullptr;
        return false;
    }
    if(swr_init(swrContext) < 0){
        std::cerr << "Failed to init audio resampler" << std::endl;
        swr_free(&swrContext);
        return false;
    }

    // ===== 3. PCM 缓冲 =====
    audioDevice = new AudioBufferDevice(audioFormat.channelCount(), audioFormat.sampleRate());
    audioDevice->openDevice();

    // ===== 4. Sink（注意：先不 start，等 play() 再启动） =====
    audioSink = new QAudioSink(device, audioFormat);
    audioSink->setBufferSize(qMax(audioFormat.bytesForDuration(200000), audioFormat.sampleRate() * audioFormat.channelCount() * 2 / 5));

    std::cout << "Audio output ready: " << audioFormat.sampleRate() << "Hz " << audioFormat.channelCount() << "ch" << std::endl;
    return true;
}


// ============================================================
// 播放
// ============================================================
void FFmpegPlayer::play(){
    {
        QMutexLocker locker(&stateMutex);
        if(!opened) return;
    }
    playing = true;

    if(audioSink){
        switch(audioSink->state()){
        case QAudio::StoppedState:
        {
            QMutexLocker locker(&clockMutex);
            audioSink->start(audioDevice);
            clockRefUsecs = audioSink->processedUSecs();   // 归零参考点
        }
        break;
        case QAudio::SuspendedState:
        case QAudio::IdleState:
            audioSink->resume();
            break;
        default:
            break;          // ActiveState
        }
    }
    else if(!wallTimer.isValid()){
        wallTimer.start();  // 无音轨：用单调墙钟兜底
    }
}


// ============================================================
// 暂停
// ============================================================
void FFmpegPlayer::pause(){
    playing = false;

    if(audioSink){
        if(audioSink->state() == QAudio::ActiveState) audioSink->suspend();
    }
    else if(wallTimer.isValid()){
        wallAccumSeconds += wallTimer.elapsed() / 1000.0;
        wallTimer.invalidate();
    }
}


// ============================================================
// 停止
// ============================================================
void FFmpegPlayer::stop(){
    playing = false;

    if(audioSink) audioSink->suspend();
    if(audioDevice) audioDevice->clearBuffer();

    flushDecoders();

    {
        QMutexLocker locker(&clockMutex);
        clockBaseSeconds = 0.0;
        clockRefUsecs    = audioSink ? audioSink->processedUSecs() : 0;
        wallAccumSeconds = 0.0;
    }
    wallTimer.invalidate();
    decodedVideoPts.store(0.0);
}


// ============================================================
// 视频解码
// ============================================================
bool FFmpegPlayer::decodeNextVideoFrame(QImage& image){
    if(!formatContext || !videoCodecContext || !packet || !videoFrame) return false;

    while(true){
        int ret = av_read_frame(formatContext, packet);
        if(ret < 0){
            endOfFile = true;          // 只有真正读到文件尾才置位
            return false;
        }

        // ===== 音频包 =====
        if(packet->stream_index == audioStreamIndex){
            decodeAudioPacket(packet);
            av_packet_unref(packet);
            continue;
        }

        // ===== 视频包 =====
        if(packet->stream_index == videoStreamIndex){
            ret = avcodec_send_packet(videoCodecContext, packet);
            av_packet_unref(packet);

            if(ret < 0 && ret != AVERROR(EAGAIN)) continue;

            while(true){
                ret = avcodec_receive_frame(videoCodecContext, videoFrame);
                if(ret == AVERROR(EAGAIN)) break;
                if(ret == AVERROR_EOF) return false;
                if(ret < 0) break;

                int64_t pts = videoFrame->best_effort_timestamp;
                if(pts == AV_NOPTS_VALUE) pts = videoFrame->pts;
                if(pts != AV_NOPTS_VALUE){
                    AVStream* stream = formatContext->streams[videoStreamIndex];
                    decodedVideoPts.store(pts * av_q2d(stream->time_base) - startTimeOffset);
                }

                if(!convertVideoFrame(videoFrame, image)) return false;
                return true;
            }
            continue;
        }

        av_packet_unref(packet);
    }
}


// ============================================================
// 视频格式转换
// ============================================================
bool FFmpegPlayer::convertVideoFrame(AVFrame* frame, QImage& image){
    if(!frame || frame->width <= 0 || frame->height <= 0) return false;

    // 分辨率/像素格式变化时重建 sws（原来只在 nullptr 时建，遇到可变分辨率会画花）
    if(!swsContext || swsSrcW != frame->width || swsSrcH != frame->height || swsSrcFmt != frame->format){
        if(swsContext){
            sws_freeContext(swsContext);
            swsContext = nullptr;
        }

        swsContext = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if(!swsContext) return false;

        swsSrcW = frame->width;
        swsSrcH = frame->height;
        swsSrcFmt = frame->format;
    }

    QImage result(frame->width, frame->height, QImage::Format_RGB32);

    uint8_t* dstData[4] = {result.bits(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {static_cast<int>(result.bytesPerLine()), 0, 0, 0};

    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);

    image = result;  // QImage 隐式共享，零拷贝；原来的 result.copy() 是纯浪费
    return true;
}


// ============================================================
// 音频Packet解码
// ============================================================
bool FFmpegPlayer::decodeAudioPacket(AVPacket* packet){
    if(!audioCodecContext || !audioFrame || !packet) return false;
    if(!swrContext || !audioDevice) return false;      // 音频链路未就绪

    int ret = avcodec_send_packet(audioCodecContext, packet);
    if(ret < 0 && ret != AVERROR(EAGAIN)) return false;

    bool decoded = false;
    while(true){
        ret = avcodec_receive_frame(audioCodecContext, audioFrame);
        if(ret < 0) break;                             // EAGAIN / EOF 都退出
        if(writeAudioFrame(audioFrame)) decoded = true;
    }
    return decoded;
}


// ============================================================
// 写入音频 （音频帧 → PCM）
// ============================================================
bool FFmpegPlayer::writeAudioFrame(AVFrame* frame){
    // 音频链路没建起来就静默跳过（不影响视频）
    if(!swrContext || !audioDevice || !audioFormat.isValid()) return true;

    const int inRate = audioCodecContext->sample_rate;
    const int outRate = audioFormat.sampleRate();
    const int outCh  = audioFormat.channelCount();
    const int bytesPerSample = 2;                     // S16

    int64_t delay = swr_get_delay(swrContext, inRate);
    int outSamples = static_cast<int>(av_rescale_rnd(delay + frame->nb_samples, outRate, inRate, AV_ROUND_UP));
    if(outSamples <= 0) return true;

    QByteArray pcm;
    pcm.resize(outSamples * outCh * bytesPerSample);

    uint8_t* out[1] = {reinterpret_cast<uint8_t*>(pcm.data())};

    int converted = swr_convert(swrContext, out, outSamples, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    if(converted <= 0) return true;

    // swr 的输出声道数 = audioFormat.channelCount()（因为 swr 就是用它建的）
    pcm.resize(converted * outCh * bytesPerSample);
    audioDevice->appendData(pcm);
    return true;
}


// ============================================================
// Seek
// 注意：这个函数现在只能由DecodeThread调用
// ============================================================
bool FFmpegPlayer::seek(double seconds){
    if(!opened || !formatContext || videoStreamIndex < 0) return false;

    double duration = getDuration();
    if(seconds < 0.0) seconds = 0.0;
    if(duration > 0.0 && seconds > duration) seconds = duration;

    AVStream* stream = formatContext->streams[videoStreamIndex];
    int64_t timestamp = static_cast<int64_t>((seconds + startTimeOffset) / av_q2d(stream->time_base));

    if(av_seek_frame(formatContext, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) return false;

    flushDecoders();

    if(audioDevice) audioDevice->clearBuffer();

    // 主时钟重置：seek 目标点成为新的时间原点
    {
        QMutexLocker locker(&clockMutex);
        clockBaseSeconds = seconds;
        clockRefUsecs = audioSink ? audioSink->processedUSecs() : 0;
        wallAccumSeconds = 0.0;
        if(wallTimer.isValid()) wallTimer.restart();
    }

    endOfFile = false;
    decodedVideoPts.store(seconds);
    return true;
}



// ============================================================
// 清空解码器
// ============================================================
void FFmpegPlayer::flushDecoders(){
    if(videoCodecContext) avcodec_flush_buffers(videoCodecContext);
    if(audioCodecContext) avcodec_flush_buffers(audioCodecContext);

    if(swrContext){
        swr_close(swrContext);
        if(swr_init(swrContext) < 0) std::cerr << "Failed to reinit resampler" << std::endl;
    }
    if(packet) av_packet_unref(packet);
}




// ============================================================
// 获取视频PTS
// ============================================================
double FFmpegPlayer::getDecodedVideoPts() const{
    return decodedVideoPts.load();
}



// ============================================================
// 获取当前播放时间
// ============================================================
double FFmpegPlayer::getCurrentTime() const{
    double t = 0.0;
    {
        QMutexLocker locker(&clockMutex);

        if(audioSink){
            // 音频时钟：processedUSecs 在 suspend 期间不增长，天然支持暂停
            double played = (audioSink->processedUSecs() - clockRefUsecs) / 1000000.0;
            if(played < 0.0) played = 0.0;
            t = clockBaseSeconds + played;
        }else{
            // 无音轨：单调墙钟兜底
            double extra = wallAccumSeconds;
            if(wallTimer.isValid()) extra += wallTimer.elapsed() / 1000.0;
            t = clockBaseSeconds + extra;
        }
    }

    double d = getDuration();
    if(d > 0.0){
        if(t < 0.0) t = 0.0;
        if(t > d) t = d;      // 防止片尾静音把进度条冲出界
    }
    return t;
}


// ============================================================
// 获取总时长
// ============================================================
double FFmpegPlayer::getDuration() const{
    return cachedDuration.load();
}


// ============================================================
// 获取帧率
// ============================================================
double FFmpegPlayer::getFrameRate() const{
    if(!formatContext || videoStreamIndex < 0) return 0.0;
    AVRational r = formatContext->streams[videoStreamIndex]->avg_frame_rate;
    if(r.den == 0 || r.num == 0) return 0.0;
    return av_q2d(r);
}


// ============================================================
// 状态
// ============================================================
bool FFmpegPlayer::isPlaying() const{
    return playing.load();
}

bool FFmpegPlayer::isOpened() const {
    QMutexLocker locker(&stateMutex);
    return opened;
}

bool FFmpegPlayer::isEndOfFile() const {
    QMutexLocker locker(&stateMutex);
    return opened && endOfFile.load();
}
