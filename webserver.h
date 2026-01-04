#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QImage>
#include <QByteArray>
#include <QMap>
#include <QTimer>

class WebServer : public QObject
{
    Q_OBJECT

public:
    explicit WebServer(QObject *parent = nullptr);
    ~WebServer();

    bool start(quint16 port = 8080);
    void stop();
    bool isRunning() const;
    quint16 serverPort() const;
    QString serverUrl() const;

    // 更新 MJPEG 串流的影格
    void updateFrame(const QImage &frame);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    void error(const QString &errorString);

private slots:
    void handleNewConnection();
    void handleClientDisconnected();
    void handleClientData();
    void sendFrameToClients();

private:
    void sendHttpResponse(QTcpSocket *socket, const QString &path);
    void sendMjpegStream(QTcpSocket *socket);
    void sendHtmlPage(QTcpSocket *socket);
    QByteArray imageToJpeg(const QImage &image, int quality = 85);

    QTcpServer *m_server;
    QList<QTcpSocket*> m_streamClients;  // 訂閱 MJPEG 串流的客戶端
    QImage m_currentFrame;  // 當前影格
    QTimer *m_frameTimer;   // 影格發送計時器
    quint16 m_port;
    bool m_isRunning;
};

#endif // WEBSERVER_H
