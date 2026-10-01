#include "FFmpegDecodeThread.h"
#include "FFmpegPlayer.h"

#include <QMutexLocker>
#include <QDebug>

FFmpegDecodeThread::FFmpegDecodeThread(FFmpegPlayer* player): player(player), stopRequested(false), playing(false),
    seekRequested(false), seekPosition(0.0), wasPlayingBeforeSeek(false){
}


FFmpegDecodeThread::~FFmpegDecodeThread(){
    stopThread();
    wait();
}


// ===== 开始播放 =====
void FFmpegDecodeThread::startPlayback(){
    {
        QMutexLocker locker(&mutex);

        playing = true;
    }

    // 唤醒正在等待播放状态的线程
    condition.wakeAll();

    // 唤醒可能因为队列满而等待的线程
    queueNotFull.wakeAll();

    if(player){
        player->play();
    }
}


// ===== 暂停播放 =====
void FFmpegDecodeThread::pausePlayback(){
    {
        QMutexLocker locker(&mutex);

        playing = false;
    }

    // 防止解码线程卡在 queueNotFull.wait()
    queueNotFull.wakeAll();

    if(player){
        player->pause();
    }
}


// ===== 请求Seek =====
void FFmpegDecodeThread::requestSeek(double seconds){
    {
        QMutexLocker locker(&mutex);

        // 记录Seek之前是否正在播放
        wasPlayingBeforeSeek = playing;

        // 暂停当前解码
        playing = false;

        seekPosition = seconds;
        seekRequested = true;
    }

    // 唤醒正常播放状态等待
    condition.wakeAll();

    // 如果线程正在等待队列空间，也必须唤醒
    queueNotFull.wakeAll();
}


// ===== 停止线程 =====
void FFmpegDecodeThread::stopThread(){
    {
        QMutexLocker locker(&mutex);

        stopRequested = true;
        playing = false;
    }

    condition.wakeAll();
    queueNotFull.wakeAll();
    //queueNotEmpty.wakeAll();
}


// ===== 判断是否要求停止 =====
bool FFmpegDecodeThread::isStopRequested(){
    QMutexLocker locker(&mutex);

    return stopRequested;
}


// ===== 清空视频队列 =====
void FFmpegDecodeThread::clearFrameQueue(){
    QMutexLocker locker(&queueMutex);

    frameQueue.clear();

    queueNotFull.wakeAll();
}


// ===== 从视频队列取出一帧 =====
bool FFmpegDecodeThread::getNextFrame(QImage& image, double& pts){
    QMutexLocker locker(&queueMutex);

    if(frameQueue.isEmpty()){
        return false;
    }

    QPair<QImage, double> frame = frameQueue.dequeue();

    image = frame.first;
    pts = frame.second;

    queueNotFull.wakeOne();

    return true;
}

bool FFmpegDecodeThread::peekNextFrame(QImage& image, double& pts){
    QMutexLocker locker(&queueMutex);

    if(frameQueue.isEmpty()) return false;

    const auto& frame = frameQueue.head();

    image = frame.first;
    pts = frame.second;

    return true;
}

bool FFmpegDecodeThread::dropNextFrame(){
    QMutexLocker locker(&queueMutex);

    if(frameQueue.isEmpty()) return false;

    frameQueue.dequeue();

    queueNotFull.wakeOne();

    return true;
}

// ===== 线程入口 =====
void FFmpegDecodeThread::run(){
    if(!player){
        emit decodeFinished();
        return;
    }

    while(true){
        // ===== 1. 停止 =====
        {
            QMutexLocker locker(&mutex);
            if(stopRequested) break;
        }

        // ===== 2. Seek =====
        bool doSeek = false;
        double target = 0.0;
        {
            QMutexLocker locker(&mutex);
            if(seekRequested){ doSeek = true; target = seekPosition; seekRequested = false; }
        }

        if(doSeek){
            bool resume = false;
            {
                QMutexLocker locker(&mutex);
                resume = wasPlayingBeforeSeek;
            }

            clearFrameQueue();

            // 不调用 player->pause()/play()：QAudioSink 只能在 GUI 线程操作。
            // 暂停与恢复由 CinemaPage 在收到 seekFinished 前后分别处理。
            player->seek(target);

            clearFrameQueue();

            {
                QMutexLocker locker(&mutex);
                playing = resume;
            }
            emit seekFinished(resume);
            continue;
        }

        // ===== 3. 等待播放状态 =====
        {
            QMutexLocker locker(&mutex);
            while(!playing && !stopRequested && !seekRequested) condition.wait(&mutex);
            if(stopRequested) break;
            if(!playing) continue;              // 被 seek 唤醒，回到顶部
        }

        // ===== 4. 队列满：用条件变量真等待，不再 msleep 空转 =====
        {
            QMutexLocker locker(&queueMutex);
            while(frameQueue.size() >= MAX_QUEUE_SIZE){
                // 50ms 超时 → 回主循环重新检查 stop / seek / playing
                if(!queueNotFull.wait(&queueMutex, 50)) break;
            }
        }

        // ===== 5. 解码一帧 =====
        QImage image;
        if(!player->decodeNextVideoFrame(image)){
            if(player->isEndOfFile()){
                // 播完了：停下来等 seek 或 stop，绝不再空转
                {
                    QMutexLocker locker(&mutex);
                    playing = false;
                }
                emit decodeFinished();
                continue;
            }
            QThread::msleep(2);                 // 仅"暂时没解出帧"才短暂让出
            continue;
        }

        double pts = player->getDecodedVideoPts();

        {
            QMutexLocker locker(&queueMutex);
            if(frameQueue.size() < MAX_QUEUE_SIZE)
                frameQueue.enqueue(qMakePair(image, pts));
        }

        emit frameAvailable();
    }

    emit decodeFinished();
}
