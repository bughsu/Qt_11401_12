QT += core gui widgets multimedia multimediawidgets network

CONFIG += c++17

# FFmpeg 8.0.1 路徑配置
FFMPEG_PATH = "C:/Users/ccsto/Downloads/ffmpeg-8.0.1-full_build-shared"

INCLUDEPATH += $${FFMPEG_PATH}/include
LIBS += -L$${FFMPEG_PATH}/lib \
        -lavformat \
        -lavcodec \
        -lavutil \
        -lswscale \
        -lswresample

# OpenCV 配置
OPENCV_PATH = "C:/opencv/build"

INCLUDEPATH += $${OPENCV_PATH}/include

LIBS += -L$${OPENCV_PATH}/x64/vc16/lib \
        -lopencv_core4100 \
        -lopencv_imgproc4100 \
        -lopencv_video4100 \
        -lopencv_videoio4100 \
        -lopencv_imgcodecs4100

SOURCES += main.cpp \
           mainwindow.cpp \
           motiondetector.cpp \
           eventlogger.cpp \
           webserver.cpp

HEADERS += mainwindow.h \
           motiondetector.h \
           eventlogger.h \
           webserver.h
