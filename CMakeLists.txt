cmake_minimum_required(VERSION 3.20)
project(CamBotTimeline VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ─── Qt6 ────────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    Network
    Sql
    Multimedia
    MultimediaWidgets
    Concurrent
)

# SerialPort — optional, required for Phase 7 (Tilta Nucleus-M FIZ control)
find_package(Qt6 OPTIONAL_COMPONENTS SerialPort)

# ─── FFmpeg (optional — graceful fallback) ──────────────────────────────────────
# Users must set FFMPEG_DIR or have pkg-config available
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS $ENV{FFMPEG_DIR}/include
          ${FFMPEG_DIR}/include
)
find_library(AVCODEC_LIBRARY    avcodec    HINTS $ENV{FFMPEG_DIR}/lib ${FFMPEG_DIR}/lib)
find_library(AVFORMAT_LIBRARY   avformat   HINTS $ENV{FFMPEG_DIR}/lib ${FFMPEG_DIR}/lib)
find_library(AVUTIL_LIBRARY     avutil     HINTS $ENV{FFMPEG_DIR}/lib ${FFMPEG_DIR}/lib)
find_library(SWSCALE_LIBRARY    swscale    HINTS $ENV{FFMPEG_DIR}/lib ${FFMPEG_DIR}/lib)

set(FFMPEG_FOUND FALSE)
if(FFMPEG_INCLUDE_DIR AND AVCODEC_LIBRARY AND AVFORMAT_LIBRARY AND AVUTIL_LIBRARY AND SWSCALE_LIBRARY)
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY})
    message(STATUS "FFmpeg found: ${FFMPEG_INCLUDE_DIR}")
else()
    message(WARNING "FFmpeg not found — RTSP camera support disabled. Set FFMPEG_DIR to enable.")
endif()

# ─── Sources (Phase 1 — Foundation) ─────────────────────────────────────────────
set(CORE_SOURCES
    src/core/command_builder.cpp
    src/core/response_parser.cpp
    src/core/feedback_parser.cpp
    src/core/byte_stream_buffer.cpp
    src/core/structured_logger.cpp
    src/core/motion_estimator.cpp
    src/core/step_response_metrics.cpp
    src/core/relay_tune.cpp
    src/core/axis_protocol.cpp
)

set(NETWORK_SOURCES
    src/network/dobot_tcp_client.cpp
    src/network/realtime_feedback_worker.cpp
    src/network/command_queue_manager.cpp
)

# ─── Sources (Phase 2+ — added as phases are completed) ─────────────────────────
# Uncomment each group as the corresponding phase is implemented.

# Phase 2 — UI Shell
set(SERVICE_SOURCES
    src/services/connection_service.cpp
    src/services/connection_state_machine.cpp
)
set(UI_SOURCES
    src/ui/main_window.cpp
    src/ui/status_bar_widget.cpp
    src/ui/teach_panel.cpp
)

# Phase 3 — Points System
list(APPEND SERVICE_SOURCES
    src/services/teach_service.cpp
    src/services/project_service.cpp
)
list(APPEND UI_SOURCES
    src/ui/camera_preview_widget.cpp
    src/ui/points_library_widget.cpp
)
set(MODEL_SOURCES
    src/models/points_model.cpp
)

# Phase 4 — Timeline
list(APPEND UI_SOURCES
    src/ui/properties_panel.cpp
    src/ui/timeline/timeline_scene.cpp
    src/ui/timeline/timeline_view.cpp
    src/ui/timeline/segment_item.cpp
    src/ui/timeline/playhead_item.cpp
    src/ui/timeline/timeline_ruler.cpp
)
list(APPEND MODEL_SOURCES
    src/models/segments_model.cpp
)

# Phase 5 — Playback
list(APPEND SERVICE_SOURCES
    src/services/playback_service.cpp
)

# Phase 9 — Path-Recording Teach Mode
list(APPEND SERVICE_SOURCES
    src/services/path_recorder_service.cpp
)

# Phase 6 — Advanced Features
list(APPEND SERVICE_SOURCES
    src/services/media_service.cpp
    src/services/export_service.cpp
)
list(APPEND UI_SOURCES
    src/ui/diagnostics_panel.cpp
    src/ui/dialogs/calibration_dialog.cpp
    src/ui/dialogs/gantry_setup_dialog.cpp
    src/ui/dialogs/gantry_tuning_dialog.cpp
    src/ui/widgets/step_response_plot.cpp
)

# Phase 7 — FIZ (Tilta Nucleus-M) — requires Qt6::SerialPort
set(FIZ_SOURCES
    src/infrastructure/fiz/nucleusprotocol.cpp
    src/infrastructure/fiz/nucleusservice.cpp
    src/application/fizservice.cpp
    src/presentation/widgets/fizpanel.cpp
    src/presentation/widgets/fiztrackwidget.cpp
    src/presentation/dialogs/fizsetupwizard.cpp
    src/presentation/dialogs/lensmappingdialog.cpp
    src/presentation/dialogs/fizkeyframedialog.cpp
)

# Phase 8 — Gantry
set(GANTRY_SOURCES
    src/infrastructure/gantry/gantryaxiscontroller.cpp
    src/infrastructure/gantry/axis_board_link.cpp
    src/infrastructure/gantry/serial_transport.cpp
    src/infrastructure/gantry/qt_serial_transport.cpp
    src/application/gantryservice.cpp
)

# Phase 9 — Timeline Engine (MRMC style)
set(TIMELINE_ENGINE_SOURCES
    src/math/motion_profile.cpp
    src/timeline/playback_engine.cpp
    src/timeline/timeline_compiler.cpp
    src/hardware/device_adapter.cpp
    src/hardware/dobot_adapter.cpp
    src/hardware/fiz_adapter.cpp
    src/hardware/gantry_adapter.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${NETWORK_SOURCES}
    ${SERVICE_SOURCES}
    ${UI_SOURCES}
    ${MODEL_SOURCES}
    ${FIZ_SOURCES}
    ${GANTRY_SOURCES}
    ${TIMELINE_ENGINE_SOURCES}
    resources/resources.qrc
)

# ─── Executable ─────────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
)

# Link SerialPort only if available
if(TARGET Qt6::SerialPort)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::SerialPort)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_SERIALPORT=1)
    message(STATUS "Qt6::SerialPort found — FIZ support enabled")
else()
    message(STATUS "Qt6::SerialPort not found — FIZ support disabled (install via Qt Maintenance Tool)")
endif()

if(FFMPEG_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_FFMPEG=1)
endif()

# ─── Install (portable ZIP layout) ─────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
install(DIRECTORY resources/style DESTINATION .)
install(DIRECTORY resources/icons DESTINATION . OPTIONAL)

# ─── Tests ──────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
