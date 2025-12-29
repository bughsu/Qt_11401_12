#include "eventlogger.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

EventLogger::EventLogger() : m_needsSave(false) {
    // 設定日誌檔案路徑
    QString appDir = QCoreApplication::applicationDirPath();
    QDir().mkpath(appDir + "/logs");
    m_logFilePath = appDir + "/logs/event_log.json";

    // 自動載入既有的日誌
    loadFromFile(m_logFilePath);

    // 建立定期保存計時器（每 30 秒保存一次）
    m_saveTimer = new QTimer(this);
    connect(m_saveTimer, &QTimer::timeout, this, [this](){
        if (m_needsSave) {
            saveToFile(m_logFilePath);
            m_needsSave = false;
        }
    });
    m_saveTimer->start(30000);  // 30 秒
}

EventLogger::~EventLogger() {
    // 確保所有未保存的事件都被寫入
    if (m_needsSave) {
        saveToFile(m_logFilePath);
    }
    delete m_saveTimer;
}

void EventLogger::logEvent(EventType type, const QString &streamUrl, const QString &description,
                            double motionLevel, const QString &snapshotPath) {
    EventRecord record;
    record.timestamp = QDateTime::currentDateTime();
    record.type = type;
    record.streamUrl = streamUrl;
    record.description = description;
    record.motionLevel = motionLevel;
    record.imageSnapshotPath = snapshotPath;

    m_events.append(record);
    m_needsSave = true;  // 標記需要保存

    qDebug() << "事件記錄:" << record.getTypeString() << "-" << description;
}

void EventLogger::forceSave() {
    if (m_needsSave) {
        saveToFile(m_logFilePath);
        m_needsSave = false;
    }
}

QList<EventRecord> EventLogger::getAllEvents() const {
    return m_events;
}

QList<EventRecord> EventLogger::getEventsByType(EventType type) const {
    QList<EventRecord> filtered;
    for (const EventRecord &record : m_events) {
        if (record.type == type) {
            filtered.append(record);
        }
    }
    return filtered;
}

QList<EventRecord> EventLogger::getEventsByTimeRange(const QDateTime &start, const QDateTime &end) const {
    QList<EventRecord> filtered;
    for (const EventRecord &record : m_events) {
        if (record.timestamp >= start && record.timestamp <= end) {
            filtered.append(record);
        }
    }
    return filtered;
}

void EventLogger::clearEvents() {
    m_events.clear();
    saveToFile(m_logFilePath);
    m_needsSave = false;
}

bool EventLogger::saveToFile(const QString &filePath) {
    QJsonArray jsonArray;
    for (const EventRecord &record : m_events) {
        jsonArray.append(record.toJson());
    }

    QJsonDocument doc(jsonArray);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "無法開啟日誌檔案以寫入:" << filePath;
        return false;
    }

    file.write(doc.toJson());
    file.close();
    return true;
}

bool EventLogger::loadFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "日誌檔案不存在，將建立新的日誌:" << filePath;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "無法開啟日誌檔案以讀取:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        qWarning() << "日誌檔案格式錯誤";
        return false;
    }

    m_events.clear();
    QJsonArray jsonArray = doc.array();
    for (const QJsonValue &value : jsonArray) {
        if (value.isObject()) {
            m_events.append(EventRecord::fromJson(value.toObject()));
        }
    }

    qDebug() << "載入" << m_events.size() << "筆事件記錄";
    return true;
}
