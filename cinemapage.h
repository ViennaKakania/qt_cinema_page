#ifndef CINEMAPAGE_H
#define CINEMAPAGE_H

#include "FFmpegPlayer.h"
class FFmpegDecodeThread;

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QLabel>
#include <QPainter>

#include <string>
using std::string;

class QPushButton;
class QLabel;
class QListWidget;
class QLineEdit;
class QSlider;


// ============================================================
// 视频画面控件
// 替换 QLabel + QPixmap::fromImage + scaled() 的组合：
//   - 不再每帧构造 QPixmap
//   - 缩放交给绘制器，一次 drawImage 完成
// ============================================================
class VideoWidget : public QWidget{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr): QWidget(parent){
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMinimumSize(640, 360);
    }

    void setFrame(const QImage& frame){
        m_frame = frame;
        update();                       // Qt 自动合并多次 update，天然限频
    }

    void clear(){
        m_frame = QImage();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override{
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if(m_frame.isNull()) return;

        QSize target = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        QRect r(QPoint(0, 0), target);
        r.moveCenter(rect().center());

        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(r, m_frame);
    }

private:
    QImage m_frame;
};


class CinemaPage: public QWidget{
    Q_OBJECT

public:
    explicit CinemaPage(QWidget* parent = nullptr);

    ~CinemaPage();

    void addChatMessage(const QString& message);
    bool isVideoReady() const { return videoReady; }

public slots:
    // 进入影院时调用；path 为空则弹文件选择框
    bool openVideo(const QString& path);
    void setVideoStateMessage(const QString& text);

signals:
    void backToRoom();
    void sendChatMsg(const QString& message);

private:
    void displayNextFrame();  // 显示帧
    void applySeek(double seconds);
    void onVideoOpened();
    void onPlaybackFinished();
    void updateTimeLabel();

private:
    QPushButton* backButton = nullptr;
    QPushButton* playButton = nullptr;
    QPushButton* openButton = nullptr;
    QPushButton* sendButton = nullptr;

    QLabel* overlayLabel = nullptr;    // 叠在视频上的提示文字
    QLabel* timeLabel = nullptr;

    QListWidget* chatList = nullptr;
    QLineEdit* messageEdit = nullptr;
    QSlider* progressSlider = nullptr;

    VideoWidget* videoWidget = nullptr;

    FFmpegPlayer* player = nullptr;
    FFmpegDecodeThread* decodeThread = nullptr;

    QTimer* playbackTimer = nullptr;
    QTimer* progressTimer = nullptr;

    bool videoReady = false;
    bool playing = false;
    bool seeking = false;
    bool userDragging = false;
};

#endif // CINEMAPAGE_H
