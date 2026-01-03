#include "webserver.h"
#include <QBuffer>
#include <QDateTime>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDebug>
#include <QMutableListIterator>

// MJPEG 串流邊界字串
static const QByteArray BOUNDARY = "--boundary";

WebServer::WebServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_port(0)
    , m_isRunning(false)
{
    // 建立影格發送計時器（30 FPS）
    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(33);  // ~30 FPS
    connect(m_frameTimer, &QTimer::timeout, this, &WebServer::sendFrameToClients);

    connect(m_server, &QTcpServer::newConnection, this, &WebServer::handleNewConnection);
}

WebServer::~WebServer()
{
    stop();
}

bool WebServer::start(quint16 port)
{
    if (m_isRunning) {
        return true;
    }

    if (!m_server->listen(QHostAddress::Any, port)) {
        emit error(QString("無法啟動伺服器: %1").arg(m_server->errorString()));
        return false;
    }

    m_port = m_server->serverPort();
    m_isRunning = true;
    m_frameTimer->start();

    qDebug() << "Web Server 已啟動於 port:" << m_port;
    emit serverStarted(m_port);
    return true;
}

void WebServer::stop()
{
    if (!m_isRunning) {
        return;
    }

    m_frameTimer->stop();

    // 斷開所有客戶端
    for (QTcpSocket *client : m_streamClients) {
        client->disconnectFromHost();
        client->deleteLater();
    }
    m_streamClients.clear();

    m_server->close();
    m_isRunning = false;
    m_port = 0;

    qDebug() << "Web Server 已停止";
    emit serverStopped();
}

bool WebServer::isRunning() const
{
    return m_isRunning;
}

quint16 WebServer::serverPort() const
{
    return m_port;
}

QString WebServer::serverUrl() const
{
    if (!m_isRunning) {
        return QString();
    }

    // 取得本機 IP 地址
    QString ipAddress = "127.0.0.1";
    QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();
    
    // 尋找第一個非 localhost 的 IPv4 地址
    for (const QHostAddress &entry : ipAddressesList) {
        if (entry != QHostAddress::LocalHost && 
            entry.toIPv4Address() && 
            !entry.isLoopback()) {
            ipAddress = entry.toString();
            break;
        }
    }

    return QString("http://%1:%2").arg(ipAddress).arg(m_port);
}

void WebServer::updateFrame(const QImage &frame)
{
    if (!frame.isNull()) {
        m_currentFrame = frame;
    }
}

void WebServer::handleNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket) {
        return;
    }

    connect(socket, &QTcpSocket::readyRead, this, &WebServer::handleClientData);
    connect(socket, &QTcpSocket::disconnected, this, &WebServer::handleClientDisconnected);

    qDebug() << "客戶端已連接:" << socket->peerAddress().toString();
}

void WebServer::handleClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    QString address = socket->peerAddress().toString();
    m_streamClients.removeAll(socket);
    socket->deleteLater();

    qDebug() << "客戶端已斷線:" << address;
    emit clientDisconnected(address);
}

void WebServer::handleClientData()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    // 讀取 HTTP 請求
    QByteArray requestData = socket->readAll();
    QString request = QString::fromUtf8(requestData);

    // 解析請求路徑
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) {
        socket->disconnectFromHost();
        return;
    }

    QStringList requestLine = lines[0].split(" ");
    if (requestLine.size() < 2) {
        socket->disconnectFromHost();
        return;
    }

    QString method = requestLine[0];
    QString path = requestLine[1];

    qDebug() << "HTTP 請求:" << method << path << "來自" << socket->peerAddress().toString();

    // 處理請求
    sendHttpResponse(socket, path);
}

void WebServer::sendHttpResponse(QTcpSocket *socket, const QString &path)
{
    if (path == "/" || path.startsWith("/index")) {
        // 發送 HTML 頁面
        sendHtmlPage(socket);
    } else if (path == "/stream.mjpeg" || path == "/stream") {
        // 發送 MJPEG 串流
        sendMjpegStream(socket);
    } else {
        // 404 Not Found
        QByteArray response = "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/plain\r\n"
                              "Connection: close\r\n\r\n"
                              "404 Not Found";
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }
}

void WebServer::sendMjpegStream(QTcpSocket *socket)
{
    // 發送 MJPEG 串流的 HTTP 標頭
    QByteArray header = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: multipart/x-mixed-replace; boundary=" + BOUNDARY + "\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: keep-alive\r\n\r\n";
    socket->write(header);
    socket->flush();

    // 將此客戶端加入串流客戶端列表
    if (!m_streamClients.contains(socket)) {
        m_streamClients.append(socket);
        emit clientConnected(socket->peerAddress().toString());
    }

    qDebug() << "MJPEG 串流客戶端已加入，目前客戶端數:" << m_streamClients.size();
}

void WebServer::sendHtmlPage(QTcpSocket *socket)
{
    QString html = R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Qt 監控系統 - 遠端監看</title>
    <style>
        body {
            font-family: 'Microsoft JhengHei', Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #f0f0f0;
            text-align: center;
        }
        h1 {
            color: #333;
            margin-bottom: 10px;
        }
        .info {
            color: #666;
            margin-bottom: 20px;
            font-size: 14px;
        }
        #stream-container {
            max-width: 100%;
            margin: 0 auto;
            background-color: #000;
            border-radius: 8px;
            overflow: hidden;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        }
        #stream {
            width: 100%;
            height: auto;
            display: block;
        }
        .status {
            margin-top: 15px;
            padding: 10px;
            background-color: #4CAF50;
            color: white;
            border-radius: 4px;
            display: inline-block;
        }
    </style>
</head>
<body>
    <h1>🎥 Qt 監控系統 - 遠端監看</h1>
    <div class="info">透過手機或平板瀏覽器即時觀看監控畫面</div>
    <div id="stream-container">
        <img id="stream" src="/stream.mjpeg" alt="Loading stream...">
    </div>
    <div class="status">● 即時串流中</div>
</body>
</html>
)";

    QByteArray htmlBytes = html.toUtf8();
    QByteArray response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Content-Length: " + QByteArray::number(htmlBytes.size()) + "\r\n"
                          "Connection: close\r\n\r\n";
    response.append(htmlBytes);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void WebServer::sendFrameToClients()
{
    if (m_currentFrame.isNull() || m_streamClients.isEmpty()) {
        return;
    }

    // 將影格轉換為 JPEG
    QByteArray jpegData = imageToJpeg(m_currentFrame);

    // 建立 MJPEG 格式的影格資料
    QByteArray frameData;
    frameData.append(BOUNDARY + "\r\n");
    frameData.append("Content-Type: image/jpeg\r\n");
    frameData.append("Content-Length: " + QByteArray::number(jpegData.size()) + "\r\n\r\n");
    frameData.append(jpegData);
    frameData.append("\r\n");

    // 發送給所有連接的客戶端
    QMutableListIterator<QTcpSocket*> it(m_streamClients);
    while (it.hasNext()) {
        QTcpSocket *socket = it.next();
        if (socket->state() == QAbstractSocket::ConnectedState) {
            qint64 written = socket->write(frameData);
            // 檢查寫入錯誤或部分寫入
            if (written == -1 || written < frameData.size()) {
                if (written == -1) {
                    qDebug() << "發送影格失敗:" << socket->errorString();
                } else {
                    qDebug() << "警告：部分寫入，預期" << frameData.size() << "位元組，實際寫入" << written << "位元組";
                }
                it.remove();
                socket->deleteLater();
            } else {
                socket->flush();
            }
        } else {
            // 客戶端已斷線，移除
            it.remove();
            socket->deleteLater();
        }
    }
}

QByteArray WebServer::imageToJpeg(const QImage &image, int quality)
{
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    
    // 將 QImage 轉換為 JPEG 格式
    image.save(&buffer, "JPEG", quality);
    
    return byteArray;
}
