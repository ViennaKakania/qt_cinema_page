#ifndef FFMPEGDECODETHREAD_H
#define FFMPEGDECODETHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>

class FFmpegPlayer;

class FFmpegDecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit FFmpegDecodeThread(FFmpegPlayer* player);
    ~FFmpegDecodeThread();

    // ===== 播放控制 =====
    void startPlayback();
    void pausePlayback();
    void requestSeek(double seconds);

    // ===== 停止线程 =====
    void stopThread();

    // ===== 从视频队列取出一帧 =====
    bool getNextFrame(QImage& image, double& pts);

signals:
    // 只作为“有新帧”的通知，不直接传图像
    void frameAvailable();

    void decodeFinished();

protected:
    void run() override;

private:
    bool isStopRequested();
    void clearFrameQueue();

private:
    FFmpegPlayer* player;

    // ===== 线程状态 =====
    QMutex mutex;
    QWaitCondition condition;

    bool stopRequested;
    bool playing;

    // ===== Seek =====
    bool seekRequested;
    double seekPosition;

    // ===== 视频帧队列 =====
    QMutex queueMutex;
    QWaitCondition queueNotFull;
    QWaitCondition queueNotEmpty;
    QQueue<QPair<QImage, double>> frameQueue;

    static constexpr int MAX_QUEUE_SIZE = 4;
};

#endif // FFMPEGDECODETHREAD_H