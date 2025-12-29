#ifndef MOTIONDETECTOR_H
#define MOTIONDETECTOR_H

#include <QObject>
#include <QImage>
#include <QTimer>
#include <opencv2/opencv.hpp>

class MotionDetector : public QObject {
    Q_OBJECT

public:
    explicit MotionDetector(QObject *parent = nullptr);
    ~MotionDetector();

    // 設定移動偵測閾值 (0.0 - 1.0)
    void setMotionThreshold(double threshold);

    // 獲取目前閾值
    double getMotionThreshold() const { return m_motionThreshold; }

    // 處理新的影像幀
    void processFrame(const QImage &frame);

    // 啟用/停用移動偵測
    void setEnabled(bool enabled);

    // 是否啟用
    bool isEnabled() const { return m_enabled; }

    // 獲取最後一次偵測的移動強度
    double getLastMotionLevel() const { return m_lastMotionLevel; }

signals:
    // 偵測到移動時發出信號
    void motionDetected(double motionLevel, const QImage &frame);

private:
    // OpenCV 背景減除器
    cv::Ptr<cv::BackgroundSubtractor> m_backgroundSubtractor;

    // 前一幀（用於差分法）
    cv::Mat m_previousFrame;

    // 移動偵測閾值
    double m_motionThreshold;

    // 是否啟用
    bool m_enabled;

    // 最後一次偵測的移動強度
    double m_lastMotionLevel;

    // 連續偵測到移動的幀數
    int m_consecutiveMotionFrames;

    // 需要連續幾幀才觸發事件（減少誤報）
    int m_minConsecutiveFrames;

    // 冷卻時間（避免重複觸發）
    int m_cooldownFrames;
    int m_currentCooldown;

    // 將 QImage 轉換為 cv::Mat
    cv::Mat qImageToMat(const QImage &image);

    // 計算移動強度
    double calculateMotionLevel(const cv::Mat &motionMask);
};

#endif // MOTIONDETECTOR_H
