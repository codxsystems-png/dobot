# ─── Tests ──────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Test Network)

# ─── Helper: add a Qt test ──────────────────────────────────────────────────────
function(add_cambot_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(${test_name} PRIVATE
        Qt6::Test
        Qt6::Network
        Qt6::Widgets
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# ─── Phase 1 Tests ──────────────────────────────────────────────────────────────

# Test: Command Builder
add_cambot_test(test_command_builder test_command_builder.cpp)

# Test: Response Parser
add_cambot_test(test_response_parser test_response_parser.cpp)
target_sources(test_response_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/response_parser.cpp
)

# Test: Feedback Parser
add_cambot_test(test_feedback_parser test_feedback_parser.cpp)
target_sources(test_feedback_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/feedback_parser.cpp
)

# Test: Byte Stream Buffer
add_cambot_test(test_byte_stream_buffer test_byte_stream_buffer.cpp)
target_sources(test_byte_stream_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/byte_stream_buffer.cpp
)

# ─── Phase 9 (Timeline Engine) — Simulation layer tests ─────────────────────────

# Test: Mock Device Adapters
add_cambot_test(test_mock_adapters test_mock_adapters.cpp)
target_sources(test_mock_adapters PRIVATE
    ${CMAKE_SOURCE_DIR}/src/hardware/device_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/mock_dobot_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/mock_fiz_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/math/motion_profile.cpp
)

# Test: Track Limits (real bounds/feasibility checking, replacing stubs)
add_cambot_test(test_track_limits test_track_limits.cpp)
target_sources(test_track_limits PRIVATE
    ${CMAKE_SOURCE_DIR}/src/math/motion_profile.cpp
)

# Test: Motion Estimator (RPM/gear-ratio -> gantry velocity/duration derivation)
add_cambot_test(test_motion_estimator test_motion_estimator.cpp)
target_sources(test_motion_estimator PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/motion_estimator.cpp
    ${CMAKE_SOURCE_DIR}/src/math/motion_profile.cpp
)

# Test: Step Response Metrics (PID tuning: overshoot / settling / rise time)
add_cambot_test(test_step_response_metrics test_step_response_metrics.cpp)
target_sources(test_step_response_metrics PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/step_response_metrics.cpp
)

# Test: Axis Board Protocol v2 (axis-addressed wire format, tagged replies)
add_cambot_test(test_axis_protocol test_axis_protocol.cpp)
target_sources(test_axis_protocol PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/axis_protocol.cpp
)

# Test: Axis Board Link (one serial port multiplexed between several axes)
add_cambot_test(test_axis_board_link test_axis_board_link.cpp)
target_sources(test_axis_board_link PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/axis_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/core/structured_logger.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/axis_board_link.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/qt_serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/fake_serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/services/connection_state_machine.cpp
)

# Test: Relay Auto-Tune (Astrom-Hagglund Ku/Tu extraction -> TL/ZN mapping)
add_cambot_test(test_relay_tune test_relay_tune.cpp)
target_sources(test_relay_tune PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/relay_tune.cpp
    ${CMAKE_SOURCE_DIR}/src/core/step_response_metrics.cpp
)

# Test: PID Controller (gantry closed-loop upgrade)
add_cambot_test(test_pid_controller test_pid_controller.cpp)

# Test: Structured Logger (persistent diagnostics)
add_cambot_test(test_structured_logger test_structured_logger.cpp)
target_sources(test_structured_logger PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/structured_logger.cpp
)

# Test: Command Queue Manager (Phase 7: ResultID lifecycle via mock transport)
add_cambot_test(test_command_queue_manager test_command_queue_manager.cpp)
target_sources(test_command_queue_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src/network/command_queue_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/network/dobot_tcp_client.cpp
    ${CMAKE_SOURCE_DIR}/src/core/response_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/command_builder.cpp
)

# Test: Timeline Compiler (Phase 7: SegmentsModel/PointsModel -> Timeline tracks)
add_cambot_test(test_timeline_compiler test_timeline_compiler.cpp)
target_sources(test_timeline_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/timeline/timeline_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/math/motion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/motion_estimator.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/device_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/models/points_model.cpp
    ${CMAKE_SOURCE_DIR}/src/models/segments_model.cpp
    ${CMAKE_SOURCE_DIR}/src/application/gantryservice.cpp
    ${CMAKE_SOURCE_DIR}/src/application/fizservice.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/gantryaxiscontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/axis_board_link.cpp
    ${CMAKE_SOURCE_DIR}/src/core/axis_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/qt_serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/services/connection_state_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/structured_logger.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/fiz/nucleusservice.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/fiz/nucleusprotocol.cpp
)

# Test: Gantry Axis Controller (Phase 7: via FakeSerialTransport, no hardware)
add_cambot_test(test_gantryaxiscontroller test_gantryaxiscontroller.cpp)
target_sources(test_gantryaxiscontroller PRIVATE
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/gantryaxiscontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/axis_board_link.cpp
    ${CMAKE_SOURCE_DIR}/src/core/axis_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/qt_serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/infrastructure/gantry/fake_serial_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/services/connection_state_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/structured_logger.cpp
)

# Test: Path Recorder Service (Phase 9: continuous-path teach)
add_cambot_test(test_path_recorder_service test_path_recorder_service.cpp)
target_sources(test_path_recorder_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src/services/path_recorder_service.cpp
)

# Test: Connection State Machine (Phase 6: fault recovery)
add_cambot_test(test_connection_state_machine test_connection_state_machine.cpp)
target_sources(test_connection_state_machine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/services/connection_state_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/structured_logger.cpp
)

# Test: PlaybackEngine robot-track closed-loop gating (Phase 5)
add_cambot_test(test_playback_engine_gating test_playback_engine_gating.cpp)
target_sources(test_playback_engine_gating PRIVATE
    ${CMAKE_SOURCE_DIR}/src/math/motion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/device_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/mock_dobot_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/timeline/playback_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/structured_logger.cpp
)
