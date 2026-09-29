#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
}

#include <QImage>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>

class AudioBufferDevice;

class FFmpegPlayer{
public:
    FFmpegPlayer();
    ~FFmpegPlayer();

    // ===== 文件操作 =====
    bool open(const char* filename);
    void close();

    // ===== 音频 =====
    bool initAudioOutput();

    // ===== 播放控制 =====
    void play();
    void pause();
    void stop();

    // ===== 解码 =====
    bool decodeNextVideoFrame(QImage& image);

    // ===== Seek =====
    bool seek(double seconds);

    // ===== 播放信息 =====
    double getDecodedVideoPts() const;

    double getCurrentTime() const;
    double getDuration() const;
    double getFrameRate() const;

    bool isPlaying() const;
    bool isOpened() const;
    //bool isEndOfFile() const;
private:
    // ===== 音频 =====
    bool decodeAudioPacket(AVPacket* packet);
    bool writeAudioFrame(AVFrame* frame);

    // ===== 视频 =====
    bool decodeVideoPacket(AVPacket* packet, QImage& image);

    bool convertVideoFrame(AVFrame* frame, QImage& image);

    // ===== FFmpeg状态 =====
    void flushDecoders();

private:
    // ===== FFmpeg文件 =====
    AVFormatContext* formatContext;

    // ===== 视频 =====
    AVCodecContext* videoCodecContext;
    const AVCodec* videoCodec;
    int videoStreamIndex;

    // ===== 音频 =====
    AVCodecContext* audioCodecContext;
    const AVCodec* audioCodec;
    int audioStreamIndex;

    // ===== 转换器 =====
    SwsContext* swsContext;
    SwrContext* swrContext;

    // ===== FFmpeg数据 =====
    AVPacket* packet;
    AVFrame* videoFrame;
    AVFrame* audioFrame;

    // ===== 播放状态 =====
    bool opened;
    bool playing;
    bool endOfFile;

    // ===== 视频时间 =====
    double decodedVideoPts;
    double currentVideoTime;

    // ===== 音频输出 =====
    QAudioFormat audioFormat;
    QAudioSink* audioSink;
    AudioBufferDevice* audioDevice;

    // ===== 音频时钟 =====
    //double audioClock;

    mutable QMutex stateMutex;
};

#endif // FFMPEGPLAYER_H