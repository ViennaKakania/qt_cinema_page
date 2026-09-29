#include "FFmpegDecodeThread.h"
#include "FFmpegPlayer.h"

#include <QMutexLocker>

FFmpegDecodeThread::FFmpegDecodeThread(FFmpegPlayer* player): player(player), stopRequested(false), playing(false), seekRequested(false), seekPosition(0.0){
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

        condition.wakeAll();
    }

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

    if(player){
        player->pause();
    }
}


// ===== 请求Seek =====
void FFmpegDecodeThread::requestSeek(double seconds){
    {
        QMutexLocker locker(&mutex);

        seekPosition = seconds;
        seekRequested = true;
    }

    condition.wakeAll();
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
    queueNotEmpty.wakeAll();
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


// ===== 线程入口 =====
void FFmpegDecodeThread::run(){
    if(!player){
        emit decodeFinished();
        return;
    }

    while(true){
        // ===== 检查停止状态 =====
        {
            QMutexLocker locker(&mutex);

            if(stopRequested){
                break;
            }
        }


        // ===== 处理Seek请求 =====
        bool doSeek = false;
        double targetPosition = 0.0;

        {
            QMutexLocker locker(&mutex);

            if(seekRequested){
                doSeek = true;
                targetPosition = seekPosition;
                seekRequested = false;
            }
        }

        if(doSeek){
            // Seek必须由解码线程执行
            player->pause();

            clearFrameQueue();

            if(!player->seek(targetPosition)){
                continue;
            }

            // Seek完成后，如果之前处于播放状态，
            // player重新进入播放状态
            {
                QMutexLocker locker(&mutex);

                if(playing){
                    player->play();
                }
            }

            continue;
        }


        // ===== 判断当前是否播放 =====
        {
            QMutexLocker locker(&mutex);

            while(!playing && !stopRequested && !seekRequested){
                condition.wait(&mutex);
            }

            if(stopRequested){
                break;
            }
        }


        // ===== 等待视频队列空间 =====
        {
            QMutexLocker locker(&queueMutex);

            while(frameQueue.size() >= MAX_QUEUE_SIZE){
                {
                    QMutexLocker stateLocker(&mutex);

                    if(stopRequested){
                        break;
                    }

                    if(!playing || seekRequested){
                        break;
                    }
                }

                queueNotFull.wait(&queueMutex);
            }
        }


        if(isStopRequested()){
            break;
        }

        // ===== 解码一帧视频 =====
        QImage image;

        if(!player->decodeNextVideoFrame(image)){
            // EOF或者暂时没有解码出帧
            QThread::msleep(2);
            continue;
        }

        double pts = player->getDecodedVideoPts();

        // ===== 放入有限队列 =====
        {
            QMutexLocker locker(&queueMutex);

            if(frameQueue.size() < MAX_QUEUE_SIZE){
                frameQueue.enqueue(qMakePair(image, pts));
            }
        }

        // ===== 通知GUI有新帧 =====
        emit frameAvailable();
    }

    emit decodeFinished();
}