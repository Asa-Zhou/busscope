QT       += core gui widgets serialport
CONFIG   += c++17 warn_on
TEMPLATE  = app
TARGET    = BusScope

DESTDIR   = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR     = $$PWD/build/moc
RCC_DIR     = $$PWD/build/rcc

INCLUDEPATH += $$PWD/src

DEFINES += QT_DEPRECATED_WARNINGS

# 源码全部以 UTF-8 保存，MinGW 下显式指定字符集，避免中文字面量乱码
win32-g++ {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
}

# 深色系统标题栏（DwmSetWindowAttribute）
win32: LIBS += -ldwmapi

# 可执行文件图标（资源管理器 / 任务栏）
win32: RC_ICONS = res/icons/busscope.ico

SOURCES += \
    src/main.cpp \
    src/app/can_page.cpp \
    src/app/frame_log_view.cpp \
    src/app/icons.cpp \
    src/app/main_window.cpp \
    src/app/poll_page.cpp \
    src/app/scope_panel.cpp \
    src/app/slave_page.cpp \
    src/app/theme.cpp \
    src/app/widget_helpers.cpp \
    src/app/wave_chart.cpp \
    src/core/can_frame_model.cpp \
    src/core/frame_log_model.cpp \
    src/core/point_file.cpp \
    src/core/poll_task.cpp \
    src/core/poll_table_model.cpp \
    src/io/can_link.cpp \
    src/io/modbus_link.cpp \
    src/io/poll_engine.cpp \
    src/proto/can/dbc.cpp \
    src/proto/modbus/crc16.cpp \
    src/proto/modbus/modbus_rtu.cpp

HEADERS += \
    src/app/can_page.h \
    src/app/frame_log_view.h \
    src/app/icons.h \
    src/app/main_window.h \
    src/app/poll_page.h \
    src/app/scope_panel.h \
    src/app/slave_page.h \
    src/app/theme.h \
    src/app/widget_helpers.h \
    src/app/wave_chart.h \
    src/core/can_frame.h \
    src/core/can_frame_model.h \
    src/core/frame.h \
    src/core/frame_log_model.h \
    src/core/point_file.h \
    src/core/poll_task.h \
    src/core/poll_table_model.h \
    src/io/can_link.h \
    src/io/modbus_link.h \
    src/io/poll_engine.h \
    src/proto/can/dbc.h \
    src/proto/modbus/crc16.h \
    src/proto/modbus/modbus_rtu.h

RESOURCES += res/res.qrc
