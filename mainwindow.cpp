#include "mainwindow.h"
#include "motiondetector.h"
#include "eventlogger.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QProcess>
#include <QFileInfo>
#include <QDebug>
#include <QThread>
#include <QVideoSink>
#include <QVideoFrame>
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // 初始化事件日誌
    m_eventLogger = new EventLogger();

    setupUi();
    resize(1200, 800);
    setWindowTitle("Qt6 專業多路監控錄影系統 - 智慧移動偵測");

    // 啟動時檢查 FFmpeg
    QTimer::singleShot(500, this, [this](){
        QProcess process;
        process.start("ffmpeg", QStringList() << "-version");
        process.waitForFinished(3000);

        if (process.error() == QProcess::FailedToStart) {
            QMessageBox::warning(this, "警告",
                                 "未偵測到 FFmpeg！\n\n"
                                 "錄影功能需要 FFmpeg 才能運作。\n"
                                 "請下載並安裝 FFmpeg：\n"
                                 "https://www.gyan.dev/ffmpeg/builds/\n\n"
                                 "下載後請將 ffmpeg.exe 放到程式目錄或加入系統 PATH。");
        }
    });
}

MainWindow::~MainWindow() {
    // 清理播放器單元
    for (PlayerUnit *unit : m_playerUnits) {
        if (unit->frameGrabTimer) {
            unit->frameGrabTimer->stop();
            delete unit->frameGrabTimer;
        }
        if (unit->motionDetector) {
            delete unit->motionDetector;
        }
    }
    qDeleteAll(m_playerUnits);
    
    // 清理事件日誌
    delete m_eventLogger;
}

void MainWindow::setupUi() {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);

    // --- 左側控制區 ---
    QWidget *leftPanel = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);

    m_streamList = new QListWidget();
    QPushButton *addBtn = new QPushButton("新增來源");
    QPushButton *playBtn = new QPushButton("開始播放");
    QPushButton *delBtn = new QPushButton("移除選定");

    m_recordBtn = new QPushButton("開啟全域錄影");
    m_recordBtn->setCheckable(true);
    m_recordBtn->setMinimumHeight(50);

    m_globalProgressBar = new QProgressBar();
    m_globalProgressBar->setVisible(false);
    m_globalProgressBar->setTextVisible(false);

    // 移動偵測控制 (新增)
    m_motionDetectBtn = new QPushButton("啟用移動偵測");
    m_motionDetectBtn->setCheckable(true);
    m_motionDetectBtn->setMinimumHeight(50);
    m_motionDetectBtn->setStyleSheet("QPushButton:checked { background-color: #4CAF50; color: white; }");

    QWidget *motionControlPanel = new QWidget();
    QVBoxLayout *motionLayout = new QVBoxLayout(motionControlPanel);
    motionLayout->setContentsMargins(5, 5, 5, 5);

    QLabel *thresholdLabel = new QLabel("靈敏度 (%):");
    m_motionThresholdSpinBox = new QSpinBox();
    m_motionThresholdSpinBox->setRange(1, 10);
    m_motionThresholdSpinBox->setValue(2);
    m_motionThresholdSpinBox->setSuffix("%");
    m_motionThresholdSpinBox->setToolTip("畫面變化超過此百分比時觸發移動偵測");

    m_autoRecordOnMotion = new QCheckBox("偵測到移動時自動錄影");
    m_autoRecordOnMotion->setToolTip("當偵測到移動時，自動開始錄影");

    motionLayout->addWidget(thresholdLabel);
    motionLayout->addWidget(m_motionThresholdSpinBox);
    motionLayout->addWidget(m_autoRecordOnMotion);

    QPushButton *mgrBtn = new QPushButton("檔案管理");
    QPushButton *eventLogBtn = new QPushButton("事件日誌");  // 新增

    leftLayout->addWidget(new QLabel("設備清單:"));
    leftLayout->addWidget(m_streamList);
    leftLayout->addWidget(addBtn);
    leftLayout->addWidget(playBtn);
    leftLayout->addWidget(delBtn);
    leftLayout->addSpacing(20);
    leftLayout->addWidget(m_recordBtn);
    leftLayout->addWidget(m_globalProgressBar);
    leftLayout->addSpacing(20);
    leftLayout->addWidget(new QLabel("智慧移動偵測:"));
    leftLayout->addWidget(m_motionDetectBtn);
    leftLayout->addWidget(motionControlPanel);
    leftLayout->addStretch();
    leftLayout->addWidget(eventLogBtn);  // 新增
    leftLayout->addWidget(mgrBtn);
    leftPanel->setFixedWidth(200);

    // --- 右側顯示區 ---
    m_stackedWidget = new QStackedWidget();

    // 頁面 0: 九宮格
    m_gridPage = new QWidget();
    m_gridLayout = new QGridLayout(m_gridPage);
    QScrollArea *scroll = new QScrollArea();
    scroll->setWidget(m_gridPage);
    scroll->setWidgetResizable(true);
    m_stackedWidget->addWidget(scroll);

    // 頁面 1: 放大畫面
    m_focusVideoWidget = new ClickableVideoWidget();
    m_focusVideoWidget->setStyleSheet("background: black;");
    m_stackedWidget->addWidget(m_focusVideoWidget);

    // 頁面 2: 管理
    m_managerPage = new QWidget();
    QVBoxLayout *manLayout = new QVBoxLayout(m_managerPage);

    // 上半部：影片列表
    m_fileListWidget = new QListWidget();
    m_fileListWidget->setMaximumHeight(200);

    // 中間：內建播放器
    QWidget *playerContainer = new QWidget();
    QVBoxLayout *playerLayout = new QVBoxLayout(playerContainer);

    m_playbackVideoWidget = new QVideoWidget();
    m_playbackVideoWidget->setStyleSheet("background: black;");
    m_playbackVideoWidget->setMinimumHeight(400);

    m_playbackPlayer = new QMediaPlayer(this);
    m_playbackAudioOutput = new QAudioOutput(this);
    m_playbackPlayer->setVideoOutput(m_playbackVideoWidget);
    m_playbackPlayer->setAudioOutput(m_playbackAudioOutput);

    // 播放控制欄
    QHBoxLayout *controlLayout = new QHBoxLayout();
    m_playBtn = new QPushButton("▶");
    m_playBtn->setFixedSize(40, 40);
    m_stopBtn = new QPushButton("■");
    m_stopBtn->setFixedSize(40, 40);
    m_positionSlider = new QSlider(Qt::Horizontal);
    m_timeLabel = new QLabel("00:00 / 00:00");
    m_timeLabel->setMinimumWidth(120);
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setMaximumWidth(100);
    QLabel *volumeIcon = new QLabel("🔊");

    controlLayout->addWidget(m_playBtn);
    controlLayout->addWidget(m_stopBtn);
    controlLayout->addWidget(m_positionSlider);
    controlLayout->addWidget(m_timeLabel);
    controlLayout->addWidget(volumeIcon);
    controlLayout->addWidget(m_volumeSlider);

    playerLayout->addWidget(m_playbackVideoWidget);
    playerLayout->addLayout(controlLayout);

    // 下半部：按鈕
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *openInExternalBtn = new QPushButton("用外部播放器開啟");
    QPushButton *openFolderBtn = new QPushButton("開啟錄影資料夾");
    QPushButton *deleteFileBtn = new QPushButton("刪除選定影片");
    QPushButton *backBtn = new QPushButton("返回監控畫面");

    btnLayout->addWidget(openInExternalBtn);
    btnLayout->addWidget(openFolderBtn);
    btnLayout->addWidget(deleteFileBtn);

    manLayout->addWidget(new QLabel("已儲存影片 (雙擊播放):"));
    manLayout->addWidget(m_fileListWidget);
    manLayout->addWidget(playerContainer);
    manLayout->addLayout(btnLayout);
    manLayout->addWidget(backBtn);
    m_stackedWidget->addWidget(m_managerPage);

    // 頁面 3: 事件日誌 (新增)
    QWidget *eventLogPage = new QWidget();
    QVBoxLayout *eventLogLayout = new QVBoxLayout(eventLogPage);

    m_eventLogTable = new QTableWidget();
    m_eventLogTable->setColumnCount(5);
    m_eventLogTable->setHorizontalHeaderLabels({"時間", "類型", "串流來源", "說明", "移動強度"});
    m_eventLogTable->horizontalHeader()->setStretchLastSection(true);
    m_eventLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventLogTable->setAlternatingRowColors(true);
    m_eventLogTable->setSortingEnabled(true);

    QHBoxLayout *eventBtnLayout = new QHBoxLayout();
    QPushButton *refreshEventBtn = new QPushButton("重新整理");
    QPushButton *clearEventBtn = new QPushButton("清除所有記錄");
    QPushButton *backFromEventBtn = new QPushButton("返回監控畫面");

    eventBtnLayout->addWidget(refreshEventBtn);
    eventBtnLayout->addWidget(clearEventBtn);
    eventBtnLayout->addStretch();

    eventLogLayout->addWidget(new QLabel("事件日誌記錄:"));
    eventLogLayout->addWidget(m_eventLogTable);
    eventLogLayout->addLayout(eventBtnLayout);
    eventLogLayout->addWidget(backFromEventBtn);
    m_stackedWidget->addWidget(eventLogPage);

    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(m_stackedWidget);

    // 連結
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::onAddStream);
    connect(playBtn, &QPushButton::clicked, this, &MainWindow::onPlaySelectedLive);
    connect(delBtn, &QPushButton::clicked, this, &MainWindow::onDeleteCamera);
    connect(m_recordBtn, &QPushButton::toggled, this, &MainWindow::onToggleGlobalRecording);
    connect(mgrBtn, &QPushButton::clicked, this, &MainWindow::switchToManagerPage);
    connect(backBtn, &QPushButton::clicked, this, [this](){
        m_playbackPlayer->stop();
        m_stackedWidget->setCurrentIndex(0);
    });
    connect(openFolderBtn, &QPushButton::clicked, this, [this](){
        QDesktopServices::openUrl(QUrl::fromLocalFile(getRecordingsPath()));
    });
    connect(openInExternalBtn, &QPushButton::clicked, this, &MainWindow::onOpenInExternalPlayer);
    connect(deleteFileBtn, &QPushButton::clicked, this, &MainWindow::onDeleteRecordedVideo);
    connect(m_fileListWidget, &QListWidget::itemDoubleClicked, this, [this](){
        onPlayRecordedVideo();
    });

    // 播放器控制
    connect(m_playBtn, &QPushButton::clicked, this, [this](){
        if (m_playbackPlayer->playbackState() == QMediaPlayer::PlayingState) {
            m_playbackPlayer->pause();
            m_playBtn->setText("▶");
        } else {
            m_playbackPlayer->play();
            m_playBtn->setText("⏸");
        }
    });

    connect(m_stopBtn, &QPushButton::clicked, this, [this](){
        m_playbackPlayer->stop();
        m_playBtn->setText("▶");
    });

    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int position){
        m_playbackPlayer->setPosition(position);
    });

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value){
        m_playbackAudioOutput->setVolume(value / 100.0);
    });

    connect(m_playbackPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 position){
        m_positionSlider->setValue(position);
        updateTimeLabel();
    });

    connect(m_playbackPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 duration){
        m_positionSlider->setRange(0, duration);
        updateTimeLabel();
    });

    connect(m_playbackPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state){
        if (state == QMediaPlayer::StoppedState) {
            m_playBtn->setText("▶");
        }
    });

    connect(m_focusVideoWidget, &ClickableVideoWidget::clicked, this, [this](){
        if (m_currentFocusedUnit) toggleFocus(m_currentFocusedUnit);
    });

    // 移動偵測相關連接 (新增)
    connect(m_motionDetectBtn, &QPushButton::toggled, this, &MainWindow::onToggleMotionDetection);

    connect(m_motionThresholdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value){
        double threshold = value / 100.0;
        for (PlayerUnit* unit : m_playerUnits) {
            if (unit->motionDetector) {
                unit->motionDetector->setMotionThreshold(threshold);
            }
        }
    });

    // 事件日誌相關連接 (新增)
    connect(eventLogBtn, &QPushButton::clicked, this, [this](){
        refreshEventLog();
        m_stackedWidget->setCurrentIndex(3);
    });

    connect(refreshEventBtn, &QPushButton::clicked, this, &MainWindow::refreshEventLog);
    connect(clearEventBtn, &QPushButton::clicked, this, &MainWindow::onClearEventLog);
    connect(backFromEventBtn, &QPushButton::clicked, this, [this](){
        m_stackedWidget->setCurrentIndex(0);
    });
}

void MainWindow::onPlaySelectedLive() {
    QListWidgetItem *item = m_streamList->currentItem();
    if (!item) return;

    PlayerUnit* unit = new PlayerUnit();
    unit->streamUrl = item->text();

    // 播放用的 Player
    unit->player = new QMediaPlayer(this);
    unit->audioOutput = new QAudioOutput(this);
    unit->videoWidget = new ClickableVideoWidget();

    unit->player->setVideoOutput(unit->videoWidget);
    unit->player->setAudioOutput(unit->audioOutput);
    unit->player->setSource(QUrl(unit->streamUrl));

    // FFmpeg 錄影進程
    unit->ffmpegProcess = nullptr;
    unit->recordingFilePath = "";

    // 初始化移動偵測器 (新增)
    unit->motionDetector = new MotionDetector(this);
    unit->motionDetector->setMotionThreshold(m_motionThresholdSpinBox->value() / 100.0);

    // 建立影格擷取計時器 (新增)
    unit->frameGrabTimer = new QTimer(this);
    
    // 連接移動偵測信號
    connect(unit->motionDetector, &MotionDetector::motionDetected, this, 
            [this, unit](double motionLevel, const QImage &frame){
        onMotionDetected(unit, motionLevel, frame);
    });

    // 使用計時器定期擷取影格進行分析（每500ms一次）
    // 注意：頻率已優化以平衡偵測效能和 CPU 使用率
    // 若需更高偵測精度，可降低至 250ms；若需降低 CPU 使用，可提高至 1000ms
    connect(unit->frameGrabTimer, &QTimer::timeout, this, [this, unit](){
        if (unit->motionDetector && unit->motionDetector->isEnabled()) {
            QVideoSink *videoSink = unit->videoWidget->videoSink();
            if (videoSink) {
                QVideoFrame frame = videoSink->videoFrame();
                if (frame.isValid()) {
                    QImage image = frame.toImage();
                    if (!image.isNull()) {
                        unit->motionDetector->processFrame(image);
                    }
                }
            }
        }
    });

    // 如果移動偵測已啟用，立即啟動
    if (m_motionDetectBtn->isChecked()) {
        unit->motionDetector->setEnabled(true);
        unit->frameGrabTimer->start(500);  // 每500ms擷取一次影格
    }

    unit->videoWidget->setMinimumSize(320, 180);
    unit->videoWidget->setStyleSheet("background: black; border: 2px solid #333;");

    connect(unit->videoWidget, &ClickableVideoWidget::clicked, this, [this, unit](){
        toggleFocus(unit);
    });

    m_playerUnits.append(unit);
    int idx = m_playerUnits.size() - 1;
    m_gridLayout->addWidget(unit->videoWidget, idx / 3, idx % 3);
    unit->player->play();

    // 記錄事件
    m_eventLogger->logEvent(EventType::StreamConnected, unit->streamUrl, 
                            "串流連接成功");
}

void MainWindow::onToggleGlobalRecording(bool checked) {
    QString path = getRecordingsPath();

    if (checked) {
        if (m_playerUnits.isEmpty()) {
            QMessageBox::warning(this, "警告", "請先新增並播放至少一個串流來源！");
            m_recordBtn->setChecked(false);
            return;
        }

        // 開始錄影 - 使用 FFmpeg
        int successCount = 0;
        QString errorLog;

        for (PlayerUnit* unit : m_playerUnits) {
            // 為每個串流生成唯一檔名
            QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            QString fileName = path + "/REC_" + timestamp + "_" + QString::number(successCount) + ".mp4";

            unit->recordingFilePath = fileName;

            // 建立 FFmpeg 進程
            unit->ffmpegProcess = new QProcess(this);

            // 設定工作目錄
            unit->ffmpegProcess->setWorkingDirectory(path);

            // FFmpeg 命令 - 根據串流類型調整
            QStringList args;

            // 判斷串流類型
            bool isRTSP = unit->streamUrl.startsWith("rtsp://", Qt::CaseInsensitive);
            bool isHTTP = unit->streamUrl.startsWith("http://", Qt::CaseInsensitive) ||
                          unit->streamUrl.startsWith("https://", Qt::CaseInsensitive);
            bool isMJPEG = isHTTP && (unit->streamUrl.contains("8081") ||
                                      unit->streamUrl.contains("mjpeg", Qt::CaseInsensitive) ||
                                      unit->streamUrl.contains("mpjpeg", Qt::CaseInsensitive));

            if (isRTSP) {
                // RTSP 串流設定
                args << "-rtsp_transport" << "tcp"
                     << "-i" << unit->streamUrl;

                // 嘗試直接複製（更快）
                args << "-c:v" << "copy"
                     << "-c:a" << "aac";

            } else if (isMJPEG) {
                // MJPEG over HTTP 串流設定
                args << "-f" << "mjpeg"               // 指定輸入格式
                     << "-i" << unit->streamUrl;

                // MJPEG 必須重新編碼
                args << "-c:v" << "libx264"
                     << "-preset" << "ultrafast"
                     << "-crf" << "23"
                     << "-pix_fmt" << "yuv420p"       // 像素格式轉換
                     << "-r" << "25";                 // 設定輸出幀率

                // 檢查是否有音訊
                args << "-c:a" << "aac"
                     << "-b:a" << "128k";

            } else if (isHTTP) {
                // HTTP/HTTPS 一般串流 (MPEG-TS, HLS 等)
                args << "-i" << unit->streamUrl;

                // 嘗試重新編碼
                args << "-c:v" << "libx264"
                     << "-preset" << "ultrafast"
                     << "-crf" << "23";

                // 如果有音訊就編碼
                args << "-c:a" << "aac"
                     << "-b:a" << "128k";
            } else {
                // 本地檔案或其他
                args << "-i" << unit->streamUrl;
                args << "-c:v" << "libx264"
                     << "-preset" << "ultrafast"
                     << "-crf" << "23"
                     << "-c:a" << "aac";
            }

            // 通用設定
            args << "-movflags" << "+faststart"
                 << "-f" << "mp4"
                 << "-t" << "3600"                    // 最長 1 小時
                 << "-y"
                 << fileName;

            qDebug() << "啟動 FFmpeg:";
            qDebug() << "串流類型:" << (isRTSP ? "RTSP" : (isMJPEG ? "MJPEG" : "HTTP"));
            qDebug() << "命令:" << "ffmpeg" << args.join(" ");
            qDebug() << "輸出檔案:" << fileName;

            // 連接輸出以便除錯
            connect(unit->ffmpegProcess, &QProcess::readyReadStandardOutput, this, [unit](){
                qDebug() << "FFmpeg 輸出:" << unit->ffmpegProcess->readAllStandardOutput();
            });

            connect(unit->ffmpegProcess, &QProcess::readyReadStandardError, this, [unit](){
                qDebug() << "FFmpeg 錯誤:" << unit->ffmpegProcess->readAllStandardError();
            });

            // 錯誤處理
            connect(unit->ffmpegProcess, &QProcess::errorOccurred, this, [this, &errorLog](QProcess::ProcessError error){
                QString errorMsg;
                switch(error) {
                case QProcess::FailedToStart:
                    errorMsg = "FFmpeg 啟動失敗！請確認已安裝 FFmpeg。\n";
                    break;
                case QProcess::Crashed:
                    errorMsg = "FFmpeg 錄影過程中崩潰！\n";
                    break;
                default:
                    errorMsg = "FFmpeg 發生未知錯誤！\n";
                }
                errorLog += errorMsg;
                qDebug() << errorMsg;
            });

            unit->ffmpegProcess->start("ffmpeg", args);

            // 等待啟動並檢查狀態
            if (unit->ffmpegProcess->waitForStarted(5000)) {
                // 等待一下確認 FFmpeg 真的在工作
                QThread::msleep(1000);

                if (unit->ffmpegProcess->state() == QProcess::Running) {
                    successCount++;
                    qDebug() << "FFmpeg 成功啟動，錄影中...";
                } else {
                    qDebug() << "FFmpeg 啟動後立即停止！";
                    errorLog += "串流 " + QString::number(successCount) + " 連接失敗\n";
                }
            } else {
                qDebug() << "FFmpeg 啟動逾時！";
                errorLog += "FFmpeg 啟動逾時\n";
            }
        }

        if (successCount == 0) {
            QString fullError = "所有錄影進程都啟動失敗！\n\n";
            fullError += errorLog.isEmpty() ? "未知錯誤" : errorLog;
            fullError += "\n請檢查：\n";
            fullError += "1. FFmpeg 是否已安裝\n";
            fullError += "2. 串流來源是否可連接\n";
            fullError += "3. 網址格式是否正確\n\n";
            fullError += "建議測試網址：\n";
            fullError += "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";

            QMessageBox::critical(this, "錯誤", fullError);
            m_recordBtn->setChecked(false);
            return;
        }

        m_recordBtn->setText(QString("停止錄影 (%1 路)").arg(successCount));
        m_recordBtn->setStyleSheet("background-color: #ff4d4d; color: white; font-weight: bold;");
        m_globalProgressBar->setVisible(true);
        m_globalProgressBar->setRange(0, 0);

        // 記錄錄影開始事件
        m_eventLogger->logEvent(EventType::RecordingStarted, "系統", 
                                QString("開始錄影 %1 路串流").arg(successCount));

    } else {
        // 停止錄影
        qDebug() << "停止錄影...";

        QStringList savedFiles;

        for (PlayerUnit* unit : m_playerUnits) {
            if (unit->ffmpegProcess && unit->ffmpegProcess->state() == QProcess::Running) {
                qDebug() << "正在停止錄影:" << unit->recordingFilePath;

                // 發送 'q' 給 FFmpeg 正常結束
                unit->ffmpegProcess->write("q\n");
                unit->ffmpegProcess->closeWriteChannel();

                if (unit->ffmpegProcess->waitForFinished(8000)) {
                    qDebug() << "FFmpeg 已正常結束";

                    // 等待檔案系統同步
                    QThread::msleep(1000);

                    // 檢查檔案是否存在
                    QFileInfo fileInfo(unit->recordingFilePath);
                    if (fileInfo.exists() && fileInfo.size() > 1024) { // 至少 1KB
                        savedFiles << fileInfo.fileName();
                        qDebug() << "檔案已儲存:" << unit->recordingFilePath
                                 << "大小:" << (fileInfo.size() / 1024.0 / 1024.0) << "MB";
                    } else {
                        qDebug() << "警告：檔案不存在或太小 (<1KB)";
                        qDebug() << "檔案路徑:" << unit->recordingFilePath;
                        qDebug() << "檔案存在:" << fileInfo.exists();
                        qDebug() << "檔案大小:" << fileInfo.size() << "bytes";
                    }
                } else {
                    qDebug() << "FFmpeg 未能正常結束，強制終止";
                    unit->ffmpegProcess->kill();
                    unit->ffmpegProcess->waitForFinished(1000);
                }

                unit->ffmpegProcess->deleteLater();
                unit->ffmpegProcess = nullptr;
            }
        }

        m_recordBtn->setText("開啟全域錄影");
        m_recordBtn->setStyleSheet("");
        m_globalProgressBar->setVisible(false);

        // 記錄錄影停止事件
        m_eventLogger->logEvent(EventType::RecordingStopped, "系統", 
                                QString("錄影已停止，共儲存 %1 個檔案").arg(savedFiles.size()));

        // 顯示結果
        if (savedFiles.isEmpty()) {
            QMessageBox::warning(this, "警告",
                                 "錄影已停止，但沒有檢測到有效的檔案！\n\n"
                                 "可能原因：\n"
                                 "1. 錄影時間太短（建議至少錄 5 秒）\n"
                                 "2. 串流連接在錄影過程中斷\n"
                                 "3. 串流格式不相容\n"
                                 "4. 磁碟空間不足或無寫入權限\n\n"
                                 "建議：\n"
                                 "• 先用測試影片網址驗證功能\n"
                                 "• 檢查 Qt Creator 的「應用程式輸出」視窗的 FFmpeg 訊息\n"
                                 "• 手動檢查 recordings 資料夾");
        } else {
            qint64 totalSize = 0;
            for (const QString &file : savedFiles) {
                QFileInfo info(getRecordingsPath() + "/" + file);
                totalSize += info.size();
            }

            QMessageBox::information(this, "成功",
                                     QString("成功儲存 %1 個影片檔案\n\n總大小：%2 MB\n\n儲存位置：\n%3")
                                         .arg(savedFiles.size())
                                         .arg(totalSize / 1024.0 / 1024.0, 0, 'f', 2)
                                         .arg(path));
        }
    }
}

void MainWindow::toggleFocus(PlayerUnit* unit) {
    if (m_stackedWidget->currentIndex() == 0) {
        m_currentFocusedUnit = unit;
        unit->player->setVideoOutput(m_focusVideoWidget);
        m_stackedWidget->setCurrentIndex(1);
    } else {
        unit->player->setVideoOutput(unit->videoWidget);
        m_currentFocusedUnit = nullptr;
        m_stackedWidget->setCurrentIndex(0);
    }
}

void MainWindow::onDeleteCamera() {
    if (m_playerUnits.isEmpty()) return;
    PlayerUnit* unit = m_playerUnits.takeLast();

    // 停止錄影
    if (unit->ffmpegProcess && unit->ffmpegProcess->state() == QProcess::Running) {
        unit->ffmpegProcess->write("q\n");
        unit->ffmpegProcess->waitForFinished(2000);
        unit->ffmpegProcess->kill();
    }

    // 停止移動偵測
    if (unit->frameGrabTimer) {
        unit->frameGrabTimer->stop();
        delete unit->frameGrabTimer;
    }
    if (unit->motionDetector) {
        delete unit->motionDetector;
    }

    // 記錄事件
    m_eventLogger->logEvent(EventType::StreamDisconnected, unit->streamUrl, "串流已移除");

    unit->player->stop();
    m_gridLayout->removeWidget(unit->videoWidget);
    unit->videoWidget->deleteLater();
    delete unit;
}

QString MainWindow::getRecordingsPath() {
    QString path = QCoreApplication::applicationDirPath() + "/recordings";
    QDir().mkpath(path);
    qDebug() << "錄影資料夾:" << path;
    return path;
}

void MainWindow::onAddStream() {
    bool ok;
    QString url = QInputDialog::getText(this, "新增串流", "請輸入網址或拖入檔案路徑:", QLineEdit::Normal, "", &ok);
    if (ok && !url.isEmpty()) m_streamList->addItem(url.trimmed());
}

void MainWindow::switchToManagerPage() {
    m_fileListWidget->clear();
    QDir dir(getRecordingsPath());
    QStringList files = dir.entryList(QStringList() << "*.mp4", QDir::Files, QDir::Time);

    if (files.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem("（尚無錄影檔案）");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled); // 禁用該項目
        m_fileListWidget->addItem(item);
    } else {
        for (const QString &file : files) {
            QFileInfo info(dir.filePath(file));
            QString displayText = QString("%1 (%2 MB)")
                                      .arg(file)
                                      .arg(info.size() / 1024.0 / 1024.0, 0, 'f', 2);
            QListWidgetItem *item = new QListWidgetItem(displayText);
            item->setData(Qt::UserRole, file); // 儲存實際檔名
            m_fileListWidget->addItem(item);
        }
    }

    m_stackedWidget->setCurrentIndex(2);
}

void MainWindow::onPlayRecordedVideo() {
    QListWidgetItem *item = m_fileListWidget->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) {
        QMessageBox::information(this, "提示", "請先選擇要播放的影片！");
        return;
    }

    QString fileName = item->data(Qt::UserRole).toString();
    QString filePath = getRecordingsPath() + "/" + fileName;

    // 檢查檔案是否存在
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, "錯誤", "影片檔案不存在！");
        return;
    }

    // 在內建播放器中播放
    m_playbackPlayer->setSource(QUrl::fromLocalFile(filePath));
    m_playbackPlayer->play();
    m_playBtn->setText("⏸");
}

void MainWindow::onOpenInExternalPlayer() {
    QListWidgetItem *item = m_fileListWidget->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) {
        QMessageBox::information(this, "提示", "請先選擇要播放的影片！");
        return;
    }

    QString fileName = item->data(Qt::UserRole).toString();
    QString filePath = getRecordingsPath() + "/" + fileName;

    // 檢查檔案是否存在
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, "錯誤", "影片檔案不存在！");
        return;
    }

    // 使用系統預設播放器開啟
    bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));

    if (!success) {
        QMessageBox::warning(this, "錯誤",
                             "無法開啟影片檔案！\n\n"
                             "請確認系統已安裝影片播放器（如 VLC、Windows Media Player）");
    }
}

void MainWindow::updateTimeLabel() {
    qint64 position = m_playbackPlayer->position();
    qint64 duration = m_playbackPlayer->duration();

    QString posStr = formatTime(position);
    QString durStr = formatTime(duration);

    m_timeLabel->setText(posStr + " / " + durStr);
}

QString MainWindow::formatTime(qint64 milliseconds) {
    int seconds = (milliseconds / 1000) % 60;
    int minutes = (milliseconds / 1000 / 60) % 60;
    int hours = (milliseconds / 1000 / 60 / 60);

    if (hours > 0) {
        return QString("%1:%2:%3")
        .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
}

void MainWindow::onDeleteRecordedVideo() {
    QListWidgetItem *item = m_fileListWidget->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) {
        QMessageBox::information(this, "提示", "請先選擇要刪除的影片！");
        return;
    }

    QString fileName = item->data(Qt::UserRole).toString();
    QString filePath = getRecordingsPath() + "/" + fileName;

    // 確認刪除
    QMessageBox::StandardButton reply = QMessageBox::question(this, "確認刪除",
                                                              QString("確定要刪除影片嗎？\n\n%1\n\n此操作無法復原！").arg(fileName),
                                                              QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (QFile::remove(filePath)) {
            QMessageBox::information(this, "成功", "影片已刪除！");
            switchToManagerPage(); // 重新整理列表
        } else {
            QMessageBox::warning(this, "錯誤", "刪除失敗！檔案可能正在使用中。");
        }
    }
}

void MainWindow::onToggleMotionDetection(bool checked) {
    if (m_playerUnits.isEmpty() && checked) {
        QMessageBox::warning(this, "警告", "請先新增並播放至少一個串流來源！");
        m_motionDetectBtn->setChecked(false);
        return;
    }

    // 啟用/停用所有串流的移動偵測
    for (PlayerUnit* unit : m_playerUnits) {
        if (unit->motionDetector) {
            unit->motionDetector->setEnabled(checked);
        }
        if (unit->frameGrabTimer) {
            if (checked) {
                unit->frameGrabTimer->start(500);  // 每500ms擷取一次影格
            } else {
                unit->frameGrabTimer->stop();
            }
        }
    }

    if (checked) {
        m_motionDetectBtn->setText("停用移動偵測");
        m_eventLogger->logEvent(EventType::MotionDetectionEnabled, "系統", "移動偵測已啟用");
    } else {
        m_motionDetectBtn->setText("啟用移動偵測");
        m_eventLogger->logEvent(EventType::MotionDetectionDisabled, "系統", "移動偵測已停用");
    }
}

void MainWindow::onMotionDetected(PlayerUnit* unit, double motionLevel, const QImage &frame) {
    QString description = QString("偵測到移動 (強度: %1%)").arg(motionLevel * 100, 0, 'f', 2);
    
    // 記錄事件
    m_eventLogger->logEvent(EventType::MotionDetected, unit->streamUrl, description, motionLevel);

    // 如果啟用了自動錄影功能且目前沒有在錄影
    if (m_autoRecordOnMotion->isChecked() && !m_recordBtn->isChecked()) {
        qDebug() << "移動偵測觸發自動錄影";
        m_recordBtn->setChecked(true);
    }

    // 可選：保存快照
    QString snapshotPath = getRecordingsPath() + "/snapshot_" + 
                           QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg";
    frame.save(snapshotPath, "JPEG");
    qDebug() << "已保存移動偵測快照:" << snapshotPath;
}

void MainWindow::refreshEventLog() {
    m_eventLogTable->setRowCount(0);
    
    QList<EventRecord> events = m_eventLogger->getAllEvents();
    
    // 倒序顯示（最新的在最上面）
    for (int i = events.size() - 1; i >= 0; --i) {
        const EventRecord &record = events[i];
        
        int row = m_eventLogTable->rowCount();
        m_eventLogTable->insertRow(row);
        
        // 時間
        m_eventLogTable->setItem(row, 0, new QTableWidgetItem(
            record.timestamp.toString("yyyy-MM-dd HH:mm:ss")));
        
        // 類型
        QTableWidgetItem *typeItem = new QTableWidgetItem(record.getTypeString());
        if (record.type == EventType::MotionDetected) {
            typeItem->setBackground(QBrush(QColor(255, 200, 200)));  // 淺紅色
        }
        m_eventLogTable->setItem(row, 1, typeItem);
        
        // 串流來源
        m_eventLogTable->setItem(row, 2, new QTableWidgetItem(record.streamUrl));
        
        // 說明
        m_eventLogTable->setItem(row, 3, new QTableWidgetItem(record.description));
        
        // 移動強度
        QString motionStr = record.motionLevel > 0 ? 
            QString("%1%").arg(record.motionLevel * 100, 0, 'f', 2) : "-";
        m_eventLogTable->setItem(row, 4, new QTableWidgetItem(motionStr));
    }
    
    // 自動調整列寬
    m_eventLogTable->resizeColumnsToContents();
}

void MainWindow::onClearEventLog() {
    QMessageBox::StandardButton reply = QMessageBox::question(this, "確認清除",
        "確定要清除所有事件記錄嗎？\n\n此操作無法復原！",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        m_eventLogger->clearEvents();
        refreshEventLog();
        QMessageBox::information(this, "成功", "事件記錄已清除！");
    }
}
