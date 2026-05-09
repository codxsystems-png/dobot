# Camera Robot Timeline — Design Specification

**Date:** 2026-05-10  
**Status:** Awaiting review  
**Deployment:** Portable ZIP — Windows 10/11 only  

---

## 1. Overview

**Camera Robot Timeline** is a professional Qt6 desktop application for programming and controlling a **Dobot Nova 5** industrial robot arm. It integrates a live camera preview, a visual timeline editor for sequencing robot movements, and real-time binary feedback — all through the Dobot's TCP/IP interface.

### Goals

- Teach robot positions visually with camera thumbnails
- Build motion sequences on a drag-and-drop timeline
- Execute sequences with ResultID-based synchronization
- Trigger camera actions (record, snapshot) at waypoints
- Ensure operator safety with E-STOP, collision detection, and mode guards

### Non-Goals (v1)

- No 3D robot visualization / kinematic simulation
- No multi-robot control
- No cloud connectivity
- No scripting engine (export only)

---

## 2. Tech Stack

| Component | Technology |
|---|---|
| Language | C++17 |
| UI Framework | Qt 6.7+ (Widgets, Network, Sql, Multimedia, MultimediaWidgets, Concurrent) |
| Build System | CMake 3.20+ |
| Database | SQLite via QtSql |
| Video Decode | FFmpeg (libavcodec, libavformat, libavutil, libswscale) |
| Timeline Render | QGraphicsView |
| Testing | GoogleTest |
| Deployment | Portable ZIP with bundled Qt/FFmpeg DLLs |

---

## 3. Canonical Data Types

These are **frozen** — never change field names, types, or struct names between sessions.

```cpp
struct JointAngles {
    double j[6];  // J1–J6 in degrees
};

struct CartesianPose {
    double x, y, z;    // millimeters
    double rx, ry, rz; // degrees
};

enum class RobotMode : int {
    Init       = 1,
    BrakeOpen  = 2,
    PowerOff   = 3,
    Disabled   = 4,
    Idle       = 5,
    Drag       = 6,
    Running    = 7,
    SingleMove = 8,
    Error      = 9,
    Pause      = 10,
    Collision  = 11
};

struct CameraPoint {
    QString      id;          // QUuid::createUuid().toString()
    QString      name;
    JointAngles  joints;
    CartesianPose pose;
    QImage       thumbnail;   // 160x90 JPEG
    QDateTime    recorded;
};

struct TimelineSegment {
    QString id;
    QString pointId;
    double  triggerTime;      // seconds from timeline start
    enum Type { MovJ, MovL, Arc } type;
    int     speedPct;         // 1-100
    int     accPct;           // 1-100
    double  cpValue;          // 0 = STOP, >0 = blend ratio
    double  blendRadius;      // mm, 0 = use cpValue instead
    double  preWait;          // seconds before move
    double  postWait;         // seconds after arrival
    QString arcViaPointId;    // empty unless type == Arc
    enum CamTrigger { None, StartRecord, StopRecord, TakePhoto } camTrigger;
    enum TriggerAt { AtStart, AtEnd, AtBoth } triggerAt;
};

struct Project {
    QString              name;
    QString              version;   // "1.1.0"
    QDateTime            created;
    QList<CameraPoint>   points;
    QList<TimelineSegment> segments;
    double               timelineDuration; // seconds
    QString              cameraSourceType; // "usb" | "ip"
    QString              cameraSourceUrl;
    QString              cameraResolution;
    int                  cameraFramerate;
};
```

---

## 4. TCP Communication Layer

> **All byte offsets, command names, parameter formats, and response formats MUST be read from `dobot/DOBOT_TCP-IP.pdf`. Do NOT guess.**

### Ports

| Port | Purpose | Protocol |
|---|---|---|
| 29999 | Dashboard commands | ASCII request/response |
| 30004 | Real-time feedback | Binary, 1440 bytes, ~8ms interval |

### Command Format

- **Send:** `"CommandName(param1,param2)\n"`
- **Response:** `"ErrorID,{v1,v2},CommandName(params);"`

### Binary Feedback (Port 30004)

Little-endian byte order. Key fields (offsets from PDF):

- Actual joint angles (6x double)
- Actual Cartesian pose (6x double)
- RobotMode (uint64)
- CurrentCommandId (uint64)
- DigitalInputs / DigitalOutputs

### Packet Reassembly — REQUIRED

TCP does not guarantee 1440-byte aligned reads. The `RealtimeFeedbackWorker` **must** implement a ring buffer / accumulator:

1. Append all incoming bytes to an internal `QByteArray`
2. Extract complete 1440-byte frames as they accumulate
3. Discard leading bytes if synchronization is lost
4. Handle socket reconnection cleanly mid-accumulation

**NEVER:** `if (data.size() == 1440) parse(data);` — this silently corrupts joint angle readings.

---

## 5. Thread Architecture (FIXED)

```
Main thread      -> Qt event loop, all UI widgets
robotThread      -> DobotTcpClient (port 29999 socket only)
feedbackThread   -> RealtimeFeedbackWorker (port 30004 only)
```

### Rules

- ALL cross-thread communication via Qt signals/slots with `Qt::QueuedConnection` ONLY
- NEVER call Qt UI methods from robotThread or feedbackThread
- NEVER share raw pointers between threads
- Worker objects use `moveToThread()` pattern, NOT `QThread` subclassing

---

## 6. Screen Layout

```
QMainWindow with QSplitter layout:

+--------------------------------------------------------------+
| Menu: File  Edit  View  Robot  Timeline  Camera              |
|       Tools  Help                                            |
| Toolbar: [New][Open][Save] | [Connect] | [! E-STOP]         |
+--------------------------------------------------------------+
| +--------------------+  +------------------------------+     |
| |  CAMERA PREVIEW    |  |  TIMELINE EDITOR             |     |
| |  QVideoWidget +    |  |  QGraphicsView               |     |
| |  overlay painter   |  |  Ruler + Track + Segments    |     |
| |                    |  |  Playhead (red vertical line)|     |
| |  [REC] [SNAP]      |  +------------------------------+     |
| |  Overlays toggles  |  |  PROPERTIES PANEL            |     |
| +--------------------+  |  (selected segment details)  |     |
|                         +------------------------------+     |
| +--------------------+  +------------------------------+     |
| |  TEACH / JOG PANEL |  |  POINTS LIBRARY              |     |
| |  Connect/Power/    |  |  QListView with thumbnails   |     |
| |  Enable buttons    |  |  Drag to timeline            |     |
| |  Drag mode toggle  |  |  [+ Record] [Create Tray]    |     |
| |  J1-J6 +/- jog     |  +------------------------------+     |
| |  XYZ RX RY RZ jog  |                                       |
| |  Step size selector|                                       |
| +--------------------+                                       |
+--------------------------------------------------------------+
| STATUS BAR (always visible, monospace font):                 |
| * Connected | Mode: IDLE | Speed: 80%                       |
| J: 000.0 045.0 -030.0 000.0 090.0 000.0                     |
| C: X:500.0 Y:0.0 Z:300.0 RX:180.0 RY:0.0 RZ:90.0           |
| Queue: ID=0 Pending:0 | CAM: *Live 1920x1080@30 REC:0:00    |
+--------------------------------------------------------------+
```

---

## 7. UI Styling

Dark theme via QSS stylesheet:

| Element | Color |
|---|---|
| Window background | `#1a1a1a` |
| Panel background | `#2d2d2d` |
| Input background | `#3a3a3a` |
| Border color | `#555555` |
| Primary text | `#e0e0e0` |
| Secondary text | `#aaaaaa` |
| Accent blue | `#0078d4` |
| Success green | `#00cc44` |
| Warning orange | `#ffaa00` |
| Error red | `#cc0000` |

### Special Elements

- **E-STOP button:** `#cc0000` bg, bold 14pt white text "E-STOP", min 120x40px, F12 shortcut, always visible, never disabled
- **Status bar numbers:** monospace 11pt
- **Timeline segments:** MovJ = `#0066cc`, MovL = `#006633`, Arc = `#cc6600`
- **Playhead:** `#ff0000`, 2px wide

---

## 8. Critical Robot Behavior Rules

### 8.1 Startup Sequence (enforce this order)

1. `RobotMode()` — check current state
2. `RequestControl()` — only if mode is 1 (Init) or 4 (Disabled)
3. `PowerOn()` -> mandatory **10-second wait**
4. `EnableRobot()` -> poll until mode reaches Idle (5)

### 8.2 Arc Command Implicit Start

`Arc(P1_via, P2_end)` uses **current robot position** as arc start — it is NOT a parameter. Before any Arc command, verify robot is at the arc's intended start. If not, insert a `MovL` to the start position first.

### 8.3 Jog Stop

- `MoveJog("J1+")` starts jogging joint 1 positive
- `MoveJog("")` (empty string) STOPS all jogging
- On button release, ALWAYS send `MoveJog("")` immediately

### 8.4 CP and Camera Triggers

When CP > 0, any IO/DOInstant between two motion commands fires DURING the transition blend, not at the waypoint. If a camera trigger must fire exactly at a position, automatically set CP = 0 on the preceding move segment during compilation.

### 8.5 ResultID Tracking (mandatory for playback)

- Every queued command returns a ResultID in the response
- Poll `GetCurrentCommandID()` every 100ms during playback
- Only advance timeline when `currentID >= segmentID` AND `RobotMode == Idle (5)`
- **NEVER** use QTimer delays to guess when a move finishes

### 8.6 Thumbnail Timing

Grab camera frame for a point thumbnail **only** after `RobotMode` returns Idle (5) — never mid-movement.

### 8.7 Collision Mode 11

Treat with same urgency as Error (mode 9). Flash red UI, show recovery dialog.

---

## 9. Safety Requirements (Non-Negotiable)

1. **E-STOP:** Always visible, F12 shortcut, sends `EmergencyStop(1)` immediately bypassing all queues. Recovery dialog: `EmergencyStop(0)` -> `ClearError()` -> `EnableRobot()`.

2. **Pre-motion mode check:** Before ANY motion command, verify `RobotMode` is Idle (5) or Running (7). Block otherwise.

3. **Collision handling:** Mode 11 -> flash red status bar, show recovery dialog, log the event.

4. **Default speed cap:** `SpeedFactor(80)` on connect. Warn if user sets >80% during teaching.

5. **Joint limit check:** Before every jog command. Block and warn if out of range.

6. **Connection loss:** If TCP socket disconnects mid-operation, immediately halt all pending commands and show reconnection dialog.

7. **Timeout guards:** If `EnableRobot()` polling doesn't reach Idle within 30 seconds, abort and show error.

---

## 10. Project File Format (.crp)

JSON format, extension `.crp`:

```json
{
  "version": "1.1.0",
  "robot_type": "dobot_nova5",
  "created": "<ISO8601>",
  "points": [
    {
      "id": "<uuid>",
      "name": "Opening Wide",
      "joints": [0.0, 45.0, -30.0, 0.0, 90.0, 0.0],
      "cartesian": [500.0, 0.0, 300.0, 180.0, 0.0, 90.0],
      "thumbnail": "<base64 JPEG>",
      "recorded": "<ISO8601>"
    }
  ],
  "segments": [
    {
      "id": "<uuid>",
      "pointId": "<uuid>",
      "triggerTime": 0.0,
      "type": "MovJ",
      "speedPct": 80,
      "accPct": 50,
      "cpValue": 0.0,
      "blendRadius": 0.0,
      "preWait": 0.0,
      "postWait": 0.5,
      "arcViaPointId": "",
      "camTrigger": "None",
      "triggerAt": "AtStart"
    }
  ],
  "timelineDuration": 30.0,
  "camera": {
    "sourceType": "usb",
    "sourceUrl": "",
    "resolution": "1920x1080",
    "framerate": 30
  }
}
```

---

## 11. Source Tree Structure

```
CamBotTimeline/
├── CMakeLists.txt
├── resources/
│   ├── style/
│   │   └── dark_theme.qss
│   ├── icons/
│   └── resources.qrc
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── types.h                    # All canonical structs/enums
│   │   ├── command_builder.h/.cpp     # ASCII command string generation
│   │   ├── response_parser.h/.cpp     # "ErrorID,{},Cmd();" parsing
│   │   ├── feedback_parser.h/.cpp     # 1440-byte binary parser
│   │   └── byte_stream_buffer.h/.cpp  # Ring buffer for packet reassembly
│   ├── network/
│   │   ├── dobot_tcp_client.h/.cpp    # QTcpSocket wrapper (port 29999)
│   │   ├── realtime_feedback_worker.h/.cpp  # Port 30004 reader
│   │   └── command_queue_manager.h/.cpp     # ResultID tracking + polling
│   ├── services/
│   │   ├── connection_service.h/.cpp  # Wires TCP layer to UI
│   │   ├── teach_service.h/.cpp       # Record point workflow
│   │   ├── playback_service.h/.cpp    # Timeline execution engine
│   │   └── project_service.h/.cpp     # .crp save/load
│   ├── ui/
│   │   ├── main_window.h/.cpp
│   │   ├── camera_preview_widget.h/.cpp
│   │   ├── teach_panel.h/.cpp
│   │   ├── points_library_widget.h/.cpp
│   │   ├── status_bar_widget.h/.cpp
│   │   ├── properties_panel.h/.cpp
│   │   └── timeline/
│   │       ├── timeline_scene.h/.cpp
│   │       ├── timeline_view.h/.cpp
│   │       ├── segment_item.h/.cpp
│   │       ├── playhead_item.h/.cpp
│   │       └── timeline_ruler.h/.cpp
│   └── models/
│       ├── points_model.h/.cpp        # QAbstractListModel
│       └── segments_model.h/.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_command_builder.cpp
│   ├── test_response_parser.cpp
│   ├── test_feedback_parser.cpp
│   ├── test_byte_stream_buffer.cpp
│   ├── test_command_queue_manager.cpp
│   ├── test_project_service.cpp
│   └── test_timeline_compiler.cpp
└── docs/
    └── superpowers/
        └── specs/
```

---

## 12. Build Order (6 Phases)

### Phase 1 — Foundation (build and test before any UI)

| Step | File(s) | Purpose |
|---|---|---|
| 1a | `CMakeLists.txt` | Project skeleton, find Qt6/FFmpeg |
| 1b | `src/core/types.h` | All canonical structs and enums |
| 1c | `src/core/command_builder.*` | Generates ASCII command strings |
| 1d | `src/core/response_parser.*` | Parses `"ErrorID,{},Cmd();"` format |
| 1e | `src/core/feedback_parser.*` | 1440-byte binary parser (offsets from PDF) |
| 1f | `src/core/byte_stream_buffer.*` | Ring buffer for packet reassembly |
| 1g | `src/network/dobot_tcp_client.*` | QTcpSocket wrapper, two connections |
| 1h | `src/network/command_queue_manager.*` | ResultID tracking + polling |

**GATE 1:** Test against real robot. Verify joint angles match DobotStudio.

### Phase 2 — UI Shell

| Step | File(s) | Purpose |
|---|---|---|
| 2a | `src/ui/main_window.*` + QSS | Skeleton layout with dark theme |
| 2b | `src/ui/status_bar_widget.*` | Live joint/cartesian display |
| 2c | `src/ui/teach_panel.*` | Jog buttons with hold/release behavior |
| 2d | `src/services/connection_service.*` | Wires TCP layer to UI |

**GATE 2:** Hold J1+ -> robot jogs -> release -> stops. Status bar updates at 60Hz.

### Phase 3 — Points System

| Step | File(s) | Purpose |
|---|---|---|
| 3a | `src/ui/camera_preview_widget.*` | USB camera first, RTSP later |
| 3b | `src/models/points_model.*` | QAbstractListModel with thumbnails |
| 3c | `src/ui/points_library_widget.*` | QListView + drag support |
| 3d | `src/services/teach_service.*` | Record point workflow |

**GATE 3:** Record point -> thumbnail captured -> save .crp -> reopen -> restored.

### Phase 4 — Timeline

| Step | File(s) | Purpose |
|---|---|---|
| 4a | `src/ui/timeline/timeline_scene.*` + `timeline_view.*` | QGraphicsView base |
| 4b | `src/ui/timeline/segment_item.*` | Colored rect items by type |
| 4c | `src/ui/timeline/playhead_item.*` | Draggable red line |
| 4d | `src/ui/timeline/timeline_ruler.*` | Time markers |
| 4e | Drag-and-drop integration | PointsLibrary -> Timeline |
| 4f | `src/ui/properties_panel.*` | Segment editing form |

**GATE 4:** Drag point -> segment appears -> click -> properties populate -> edit -> segment updates.

### Phase 5 — Playback

| Step | File(s) | Purpose |
|---|---|---|
| 5a | `src/services/playback_service.*` (compiler) | Segments -> command list |
| 5b | `src/services/playback_service.*` (executor) | Execution + ResultID sync |
| 5c | Playback controls | Play/Pause/Stop/Loop buttons |
| 5d | Path recovery | Pause -> jog -> resume |

**GATE 5:** 3-point MovJ sequence executes. Playhead tracks via ResultID. Pause/resume works.

### Phase 6 — Advanced Features

| Step | Purpose |
|---|---|
| 6a | Arc segment full support (3-point compiler logic) |
| 6b | Camera recording triggers in timeline |
| 6c | IP/RTSP camera via FFmpeg |
| 6d | Project save/load (complete .crp format) |
| 6e | Export: CSV, Python script, command list |
| 6f | Calibration dialog (SetTool / SetUser) |
| 6g | Tray grid dialog (CreateTray / GetTrayPoint) |
| 6h | Diagnostics panel (log export, force monitor) |

---

## 13. Verification Gates

| Gate | After | Checks |
|---|---|---|
| **Gate 1** | Phase 1 | TCP connects 29999+30004; joint angles match DobotStudio; partial packet reassembly tested; E-STOP works |
| **Gate 2** | Phase 2 | Jog hold/release works; status bar 60Hz; connection indicator correct |
| **Gate 3** | Phase 3 | Record point with thumbnail; save/reload .crp with thumbnail |
| **Gate 4** | Phase 4 | Drag-drop creates segment; selection populates properties; edit updates segment |
| **Gate 5** | Phase 5 | 3-point MovJ sequence; playhead tracks ResultID; pause/resume works |

---

## 14. Deployment

- **Platform:** Windows 10/11 only
- **Distribution:** Portable ZIP
- **Contents:** `CamBotTimeline.exe` + Qt6 DLLs + FFmpeg DLLs + `platforms/` plugin + `styles/` + `resources/`
- **CMake target:** `install` copies all dependencies to a portable output directory
- **No installer required** — unzip and run

---

## 15. Constraints and Assumptions

1. **PDF is ground truth** for all Dobot-specific details (byte offsets, command parameters, error codes)
2. **Assumptions** marked with `// ASSUMPTION: verify against hardware` in code
3. **Every .cpp gets a corresponding test file** in `tests/` using GoogleTest
4. **No code generation** — all struct definitions are hand-written in `types.h`
5. **Qt6 must be pre-installed** on the build machine (not vendored)
6. **FFmpeg must be pre-built** — use pre-compiled Windows binaries from gyan.dev or BtbN
