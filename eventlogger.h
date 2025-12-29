#ifndef EVENTLOGGER_H
#define EVENTLOGGER_H

#include <QString>
#include <QDateTime>
#include <QList>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

// 事件類型
enum class EventType {
    MotionDetected,
    MotionDetectionEnabled,      // 新增
    MotionDetectionDisabled,     // 新增
    RecordingStarted,
    RecordingStopped,
    StreamConnected,
    StreamDisconnected
};

// 事件記錄結構
struct EventRecord {
    QDateTime timestamp;
    EventType type;
    QString streamUrl;
    QString description;
    QString imageSnapshotPath;  // 可選的快照圖片路徑
    double motionLevel;         // 移動強度 (0.0-1.0)

    // 轉換為 JSON
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        obj["type"] = static_cast<int>(type);
        obj["streamUrl"] = streamUrl;
        obj["description"] = description;
        obj["imageSnapshotPath"] = imageSnapshotPath;
        obj["motionLevel"] = motionLevel;
        return obj;
    }

    // 從 JSON 轉換
    static EventRecord fromJson(const QJsonObject &obj) {
        EventRecord record;
        record.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        record.type = static_cast<EventType>(obj["type"].toInt());
        record.streamUrl = obj["streamUrl"].toString();
        record.description = obj["description"].toString();
        record.imageSnapshotPath = obj["imageSnapshotPath"].toString();
        record.motionLevel = obj["motionLevel"].toDouble();
        return record;
    }

    // 獲取事件類型文字
    QString getTypeString() const {
        switch(type) {
        case EventType::MotionDetected: return "移動偵測";
        case EventType::MotionDetectionEnabled: return "啟用移動偵測";
        case EventType::MotionDetectionDisabled: return "停用移動偵測";
        case EventType::RecordingStarted: return "開始錄影";
        case EventType::RecordingStopped: return "停止錄影";
        case EventType::StreamConnected: return "串流連接";
        case EventType::StreamDisconnected: return "串流斷線";
        default: return "未知事件";
        }
    }
};

// 事件日誌管理器
class EventLogger {
public:
    EventLogger();
    ~EventLogger();

    // 記錄事件
    void logEvent(EventType type, const QString &streamUrl, const QString &description,
                  double motionLevel = 0.0, const QString &snapshotPath = "");

    // 獲取所有事件
    QList<EventRecord> getAllEvents() const;

    // 獲取特定類型的事件
    QList<EventRecord> getEventsByType(EventType type) const;

    // 獲取特定時間範圍的事件
    QList<EventRecord> getEventsByTimeRange(const QDateTime &start, const QDateTime &end) const;

    // 清除所有事件
    void clearEvents();

    // 保存到檔案
    bool saveToFile(const QString &filePath);

    // 從檔案載入
    bool loadFromFile(const QString &filePath);

    // 強制保存（立即寫入）
    void forceSave();

    // 獲取事件數量
    int getEventCount() const { return m_events.size(); }

private:
    QList<EventRecord> m_events;
    QString m_logFilePath;
    bool m_needsSave;  // 新增：標記是否需要保存
    QTimer *m_saveTimer;  // 新增：定期保存計時器
};

#endif // EVENTLOGGER_H
