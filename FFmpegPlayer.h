#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <QImage>
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QMutex>
#include <QElapsedTimer>

#include <atomic>

class AudioBufferDevice;

class FFmpegPlayer{
public:
    FFmpegPlayer();
    ~FFmpegPlayer();

    bool open(const char* filename);          // filename 必须是 UTF-8
    void close();                             // 必须由“解码线程已停止”之后调用

    bool initAudioOutput();                   // 仅 GUI 线程调用

    void play();                              // 仅 GUI 线程调用（操作 QAudioSink）
    void pause();                             // 仅 GUI 线程调用
    void stop();                              // 仅 GUI 线程调用

    bool decodeNextVideoFrame(QImage& image); // 仅解码线程调用
    bool seek(double seconds);                // 仅解码线程调用

    double getCurrentTime() const;            // 主时钟（秒），任意线程安全
    double getDuration() const;
    double getFrameRate() const;

    bool isPlaying() const;
    bool isOpened() const;
    bool isEndOfFile() const;

    double getDecodedVideoPts() const;

private:
    bool decodeAudioPacket(AVPacket* packet);
    bool writeAudioFrame(AVFrame* frame);
    bool convertVideoFrame(AVFrame* frame, QImage& image);
    void flushDecoders();

private:
    AVFormatContext* formatContext = nullptr;

    AVCodecContext* videoCodecContext = nullptr;
    const AVCodec* videoCodec = nullptr;
    int videoStreamIndex = -1;

    AVCodecContext* audioCodecContext = nullptr;
    const AVCodec* audioCodec = nullptr;
    int audioStreamIndex = -1;

    SwsContext* swsContext = nullptr;
    int swsSrcW = 0, swsSrcH = 0, swsSrcFmt = -1;

    SwrContext* swrContext = nullptr;

    AVPacket* packet = nullptr;
    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;

    bool opened = false;                  // 由 stateMutex 保护
    std::atomic<bool> playing{false};
    std::atomic<bool> endOfFile{false};

    double startTimeOffset = 0.0;                    // 文件首 PTS 不为 0 时的偏移
    std::atomic<double> cachedDuration{0.0};
    std::atomic<double> decodedVideoPts{0.0};

    // ===== 主时钟 =====
    // 有音频： clock = clockBaseSeconds + (processedUSecs() - clockRefUsecs) / 1e6
    // 无音频： clock = clockBaseSeconds + wallAccumSeconds + wallTimer.elapsed()
    mutable QMutex clockMutex;
    double clockBaseSeconds = 0.0;
    qint64 clockRefUsecs = 0;
    double wallAccumSeconds = 0.0;
    QElapsedTimer wallTimer;

    // ===== 音频输出 =====
    QAudioFormat audioFormat;
    QAudioSink* audioSink = nullptr;
    AudioBufferDevice* audioDevice = nullptr;

    mutable QMutex stateMutex;
};

#endif // FFMPEGPLAYER_H

