#include "motiondetector.h"
#include <QDebug>

MotionDetector::MotionDetector(QObject *parent)
    : QObject(parent)
    , m_motionThreshold(0.02)  // 預設 2% 的像素變化才觸發
    , m_enabled(false)
    , m_lastMotionLevel(0.0)
    , m_consecutiveMotionFrames(0)
    , m_minConsecutiveFrames(3)  // 需要連續 3 幀才觸發，減少誤報
    , m_cooldownFrames(10)       // 觸發後冷卻 10 幀（約 5 秒）
    , m_currentCooldown(0)
{
    // 建立 MOG2 背景減除器（推薦用於移動偵測）
    m_backgroundSubtractor = cv::createBackgroundSubtractorMOG2(500, 16.0, true);
}

MotionDetector::~MotionDetector() {
}

void MotionDetector::setMotionThreshold(double threshold) {
    m_motionThreshold = qBound(0.0, threshold, 1.0);
}

void MotionDetector::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        // 重置狀態
        m_previousFrame.release();
        m_consecutiveMotionFrames = 0;
        m_currentCooldown = 0;
        m_lastMotionLevel = 0.0;
    }
}

void MotionDetector::processFrame(const QImage &frame) {
    if (!m_enabled || frame.isNull()) {
        return;
    }

    // 將 QImage 轉換為 OpenCV Mat
    cv::Mat currentFrame = qImageToMat(frame);
    if (currentFrame.empty()) {
        return;
    }

    // 轉換為灰階
    cv::Mat grayFrame;
    if (currentFrame.channels() == 3) {
        cv::cvtColor(currentFrame, grayFrame, cv::COLOR_BGR2GRAY);
    } else if (currentFrame.channels() == 4) {
        cv::cvtColor(currentFrame, grayFrame, cv::COLOR_BGRA2GRAY);
    } else {
        grayFrame = currentFrame;
    }

    // 高斯模糊以減少噪音
    cv::GaussianBlur(grayFrame, grayFrame, cv::Size(21, 21), 0);

    // 使用背景減除器
    cv::Mat foregroundMask;
    m_backgroundSubtractor->apply(grayFrame, foregroundMask);

    // 形態學操作以去除噪音
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(foregroundMask, foregroundMask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(foregroundMask, foregroundMask, cv::MORPH_CLOSE, kernel);

    // 計算移動強度
    double motionLevel = calculateMotionLevel(foregroundMask);
    m_lastMotionLevel = motionLevel;

    // 如果在冷卻期間，遞減計數器
    if (m_currentCooldown > 0) {
        m_currentCooldown--;
        return;
    }

    // 判斷是否超過閾值
    if (motionLevel > m_motionThreshold) {
        m_consecutiveMotionFrames++;

        // 連續幾幀都偵測到移動才觸發事件
        if (m_consecutiveMotionFrames >= m_minConsecutiveFrames) {
            qDebug() << "移動偵測觸發！強度:" << motionLevel;
            emit motionDetected(motionLevel, frame);
            
            // 進入冷卻期，避免同一個移動事件重複觸發
            m_consecutiveMotionFrames = 0;
            m_currentCooldown = m_cooldownFrames;
        }
    } else {
        // 重置計數器
        m_consecutiveMotionFrames = 0;
    }
}

cv::Mat MotionDetector::qImageToMat(const QImage &image) {
    cv::Mat mat;
    
    switch (image.format()) {
    case QImage::Format_RGB888: {
        QImage swapped = image.rgbSwapped();
        mat = cv::Mat(swapped.height(), swapped.width(), CV_8UC3,
                      const_cast<uchar*>(swapped.bits()), swapped.bytesPerLine()).clone();
        break;
    }
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied: {
        mat = cv::Mat(image.height(), image.width(), CV_8UC4,
                      const_cast<uchar*>(image.bits()), image.bytesPerLine()).clone();
        break;
    }
    case QImage::Format_Grayscale8: {
        mat = cv::Mat(image.height(), image.width(), CV_8UC1,
                      const_cast<uchar*>(image.bits()), image.bytesPerLine()).clone();
        break;
    }
    default: {
        // 轉換為 RGB888 格式
        QImage converted = image.convertToFormat(QImage::Format_RGB888);
        QImage swapped = converted.rgbSwapped();
        mat = cv::Mat(swapped.height(), swapped.width(), CV_8UC3,
                      const_cast<uchar*>(swapped.bits()), swapped.bytesPerLine()).clone();
        break;
    }
    }
    
    return mat;
}

double MotionDetector::calculateMotionLevel(const cv::Mat &motionMask) {
    if (motionMask.empty()) {
        return 0.0;
    }

    // 計算前景像素的比例
    int totalPixels = motionMask.rows * motionMask.cols;
    int motionPixels = cv::countNonZero(motionMask);
    
    double ratio = static_cast<double>(motionPixels) / static_cast<double>(totalPixels);
    
    return ratio;
}
