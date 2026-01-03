# 開發者快速參考

## 專案結構

```
Qt_11401_12/
├── main.cpp                  # 程式入口點
├── mainwindow.h/cpp          # 主視窗（UI 和整合邏輯）
├── motiondetector.h/cpp      # 移動偵測模組（OpenCV）
├── eventlogger.h/cpp         # 事件日誌模組（JSON 儲存）
├── Topic.pro                 # Qt 專案設定檔
└── README.md / FEATURES.md   # 文檔
```

## 核心類別

### EventLogger（事件日誌管理器）
**職責**：記錄和管理系統事件
**繼承**：QObject（支援 Qt 信號槽機制和計時器）

**關鍵功能**：
- `logEvent()` - 記錄新事件
- `getAllEvents()` - 取得所有事件
- `saveToFile()` - 儲存事件到 JSON
- `loadFromFile()` - 從 JSON 載入事件

**優化**：
- 批次寫入（每 30 秒自動保存）
- 程式關閉時強制保存

### MotionDetector（移動偵測器）
**職責**：分析視訊影格並偵測移動

**演算法流程**：
1. QImage → cv::Mat 轉換
2. 轉為灰階
3. 高斯模糊（降噪）
4. MOG2 背景減除
5. 形態學操作（去除小雜訊）
6. 計算前景像素比例
7. 與閾值比較並觸發事件

**關鍵參數**：
- `m_motionThreshold` - 偵測閾值（預設 2%）
- `m_minConsecutiveFrames` - 需連續幾幀才觸發（預設 3）
- `m_cooldownFrames` - 冷卻期幀數（預設 10，約 5 秒）

**信號**：
- `motionDetected(double motionLevel, const QImage &frame)` - 偵測到移動時發出

### MainWindow（主視窗）
**職責**：整合所有功能並提供 UI

**PlayerUnit 結構**：
```cpp
struct PlayerUnit {
    QString streamUrl;              // 串流 URL
    QMediaPlayer *player;           // Qt 播放器
    ClickableVideoWidget *videoWidget;  // 視訊顯示
    MotionDetector *motionDetector; // 移動偵測器
    QTimer *frameGrabTimer;         // 影格擷取計時器（500ms）
    QProcess *ffmpegProcess;        // FFmpeg 錄影進程
    // ...
};
```

**頁面管理**（QStackedWidget）：
- 頁面 0：九宮格監控畫面
- 頁面 1：單一畫面放大
- 頁面 2：錄影檔案管理
- 頁面 3：事件日誌查看

## 事件類型

```cpp
enum class EventType {
    MotionDetected,              // 偵測到移動
    MotionDetectionEnabled,      // 啟用移動偵測
    MotionDetectionDisabled,     // 停用移動偵測
    RecordingStarted,            // 開始錄影
    RecordingStopped,            // 停止錄影
    StreamConnected,             // 串流連接
    StreamDisconnected           // 串流斷線
};
```

## 資料流

### 移動偵測資料流
```
QMediaPlayer → QVideoWidget → QVideoSink
                                  ↓
                            QTimer (500ms)
                                  ↓
                          QVideoFrame::toImage()
                                  ↓
                          MotionDetector::processFrame()
                                  ↓
                          OpenCV 影像分析
                                  ↓
                          motionDetected 信號
                                  ↓
                          MainWindow::onMotionDetected()
                                  ↓
                    ┌──────────────┴──────────────┐
                    ↓                             ↓
            EventLogger::logEvent()      自動錄影（可選）
                    ↓
            JSON 檔案（批次保存）
```

### 錄影資料流
```
串流來源 → FFmpeg 進程 → MP4 檔案
                           ↓
                   recordings/ 資料夾
                           ↓
                   檔案管理頁面顯示
```

## 效能考量

### CPU 使用優化
- 影格擷取頻率：500ms（可調整）
- 連續確認機制：需 3 幀確認才觸發
- 冷卻期：觸發後 10 幀內不重複偵測

### I/O 優化
- 事件日誌批次寫入（30 秒）
- 程式關閉時強制保存
- 使用 JSON 格式便於閱讀和除錯

### 記憶體管理
- PlayerUnit 使用指標，由 MainWindow 管理生命週期
- Qt 父子物件自動管理記憶體
- 影格處理使用 OpenCV Mat clone() 避免資料競爭

## 編譯需求

### Windows
```bash
# Qt 6.x
# FFmpeg 8.0.1+
# OpenCV 4.x (編譯時需要 opencv_world4100.dll)

# 設定環境變數或修改 Topic.pro
FFMPEG_PATH = "C:/path/to/ffmpeg"
OPENCV_PATH = "C:/path/to/opencv/build"
```

### 必要的 DLL（執行時）
- Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll
- Qt6Multimedia.dll, Qt6MultimediaWidgets.dll
- opencv_world4100.dll（或分別的 OpenCV DLL）
- avformat.dll, avcodec.dll, avutil.dll, swscale.dll（FFmpeg）

## 除錯技巧

### 啟用 Qt 除錯訊息
```cpp
qDebug() << "訊息";  // 已在關鍵點加入除錯訊息
```

### 查看事件日誌
```bash
# 位置：<app_dir>/logs/event_log.json
cat logs/event_log.json | jq .  # 使用 jq 格式化 JSON
```

### 查看 FFmpeg 輸出
- 已連接 FFmpeg 的 stdout/stderr 到 qDebug()
- 在 Qt Creator 的「應用程式輸出」視窗查看

### 常見問題

**Q: 移動偵測不觸發？**
A: 
1. 檢查靈敏度設定（降低閾值）
2. 確認串流畫面有實際變化
3. 查看 qDebug() 輸出的移動強度值

**Q: 錄影檔案無法播放？**
A:
1. 確認錄影時間足夠長（至少 5 秒）
2. 檢查檔案大小（應 > 1KB）
3. 查看 FFmpeg 錯誤訊息

**Q: OpenCV 載入失敗？**
A:
1. 確認 DLL 在 PATH 或程式目錄
2. 檢查 OpenCV 版本與編譯器相容
3. 確認 Topic.pro 中的路徑正確

## 擴充功能建議

### 新增偵測演算法
在 `MotionDetector` 中新增方法：
```cpp
void MotionDetector::processFrameWithYOLO(const QImage &frame) {
    // 使用 YOLO 進行物件偵測
}
```

### 新增事件類型
在 `eventlogger.h` 中新增：
```cpp
enum class EventType {
    // ... 現有類型
    ObjectDetected,      // 物件偵測
    FaceRecognized,      // 人臉辨識
};
```

### 新增通知功能
在 `MainWindow` 中：
```cpp
void MainWindow::onMotionDetected(...) {
    // ... 現有程式碼
    sendEmailNotification();  // 發送郵件
    showSystemNotification(); // 系統通知
}
```

## 授權與貢獻

本專案為教育用途。歡迎貢獻！

## 技術支援

如有問題，請參考：
- [FEATURES.md](FEATURES.md) - 完整功能說明
- [README.md](README.md) - 快速入門
- Qt 6 文檔：https://doc.qt.io/qt-6/
- OpenCV 文檔：https://docs.opencv.org/
