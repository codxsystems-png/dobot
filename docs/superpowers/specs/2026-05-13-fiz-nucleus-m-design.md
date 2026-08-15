# FIZ Nucleus-M Control — Design Addendum

**Date:** 2026-05-13  
**Status:** Approved for implementation  
**Base Spec:** `2026-05-10-camera-robot-timeline-design.md`  
**Feature:** Tilta Nucleus-M FIZ (Focus / Iris / Zoom) lens control  

---

## 1. Overview

Adds full Tilta Nucleus-M FIZ lens control running **in parallel** with the existing robot motion timeline. Three motors daisy-chained over a single USB-to-TTL serial adapter are independently addressable via a community-reverse-engineered 8-byte packet protocol.

**Key principle:** FIZ and robot motion are completely decoupled transports.  
- Robot → TCP (port 29999 / 30004), event-driven via ResultID  
- FIZ   → Serial (USB-TTL 115200 baud), 30 Hz timer-driven interpolation

---

## 2. Hardware

```
PC USB  →  Waveshare USB-to-TTL-D (3.3 V jumper!)
        →  7-pin LEMO cable
        →  Motor 1 (Focus, purple LED)  Port A
           Motor 1 Port B  →  Motor 2 (Iris, green LED)  Port A
           Motor 2 Port B  →  Motor 3 (Zoom, blue LED)   Port A
External battery D-Tap  →  Motor 1 (powers the whole chain)
```

**CRITICAL:** Waveshare USB-to-TTL-D MUST have its voltage jumper set to **3.3 V**.  
5 V will permanently destroy the Nucleus-M motors. UI enforces a one-time confirmation.

---

## 3. Serial Settings

| Parameter    | Value   |
|---|---|
| Baud rate    | 115200  |
| Data bits    | 8       |
| Stop bits    | 1       |
| Parity       | None    |
| Flow control | None    |
| Qt class     | QSerialPort (Qt6::SerialPort) |

---

## 4. Protocol — 8-byte Packet

```
Byte 0:  0x3A           Start marker (ASCII colon `:`)
Byte 1:  CMD_HIGH       Command type high byte
Byte 2:  CMD_LOW        Command type low byte
Byte 3:  MOTOR_ID       0x01=Focus  0x02=Iris  0x03=Zoom
Byte 4:  FLAGS          0x75 for move commands
Byte 5:  VALUE_HIGH     Position high byte
Byte 6:  VALUE_LOW      Position low byte
Byte 7:  CHECKSUM       (byte1+byte2+byte3+byte4+byte5+byte6) & 0xFF
         // ASSUMPTION: verify against logic analyser output
```

### Motor IDs

| ID   | Motor | LED Color |
|---|---|---|
| 0x01 | Focus | Purple |
| 0x02 | Iris  | Green  |
| 0x03 | Zoom  | Blue   |

### Value Range

| Hex    | Meaning |
|---|---|
| 0x0000 | 0%   — close focus / min iris / wide zoom |
| 0x7FFF | 100% — infinity   / max iris / tele zoom  |

### Known Packets

**Motor Move**
```
[0x3A, 0x01, 0x06, MOTOR_ID, 0x75, VALUE_HIGH, VALUE_LOW, CHECKSUM]
```

**Heartbeat** (send every 1000 ms; motors disengage ~1500 ms after last heartbeat)
```
Raw hex: 3A 96 06 00 02 00 01 61
// ASSUMPTION: verify against logic analyser output
```

**Calibrate Motor**
```
[0x3A, 0x01, 0x07, MOTOR_ID, 0x00, 0x00, 0x00, CHECKSUM]
```

**Inter-packet delay on daisy chain:** wait 2 ms between each of the 3 motor packets  
(prevents bus collision; total serial time ≈ 6 ms within the 33 ms / 30 Hz budget)

---

## 5. New Data Types (add to `src/core/types.h`)

```cpp
// FIZ state — all values 0.0–100.0 percent
struct FizState {
    float focus = 0.0f;   // 0% = close focus, 100% = infinity
    float iris  = 0.0f;   // 0% = wide open,   100% = closed
    float zoom  = 0.0f;   // 0% = wide angle,  100% = telephoto
};

// FIZ keyframe on the timeline
struct FizKeyframe {
    QString  id;
    double   time;          // seconds from timeline start
    FizState state;

    enum class Easing {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut
    } easing = Easing::Linear;
};

// Optional real-world lens mapping (UI display only — stored as mm internally)
struct LensMapping {
    float   focusNearMm = 300.0f;   // closest focus distance in mm
    float   focusFarMm  = 5000.0f;  // furthest focus distance in mm
    float   zoomWideMm  = 24.0f;    // wide end focal length in mm
    float   zoomTeleMm  = 85.0f;    // tele end focal length in mm
    QString lensName    = "";       // e.g. "Canon 24-85mm f/3.5"
};

// Additions to existing structs:
//   CameraPoint  += FizState  fizState;
//   Project      += QList<FizKeyframe> fizKeyframes;
//   Project      += LensMapping        lensMapping;
```

---

## 6. New Source Files

### 6.1 `src/infrastructure/fiz/nucleusprotocol.h`

Static helper — pure packet building, no QObject.

```cpp
class NucleusProtocol {
public:
    static QByteArray focusCommand(float percent);
    static QByteArray irisCommand(float percent);
    static QByteArray zoomCommand(float percent);
    static QByteArray calibrateCommand(uint8_t motorId);
    static QByteArray heartbeatPacket();
    static uint16_t   percentToRaw(float percent);   // 0–100 → 0x0000–0x7FFF
    static float      rawToPercent(uint16_t raw);    // reverse
    static uint8_t    calculateChecksum(const QByteArray& pkt); // bytes 1–6
};
```

### 6.2 `src/infrastructure/fiz/nucleusservice.h/.cpp`

```
class NucleusService : public QObject
  Thread:      moved to its own QThread via moveToThread()
  Transport:   QSerialPort*
  Heartbeat:   QTimer* (1000 ms) — runs even during pause, stops only on disconnect
```

Public API:
```cpp
QStringList availablePorts() const;
bool        connect(const QString& portName);
void        disconnect();
bool        isConnected() const;
FizState    currentState() const;
void        setFocus(float percent);
void        setIris(float percent);
void        setZoom(float percent);
void        sendFizFrame(const FizState& state); // all 3 motors, 2 ms gaps
void        calibrateMotor(uint8_t motorId);
void        calibrateAll();
void        setLensMapping(const LensMapping& m);
float       focusPercentToMm(float pct) const;
float       zoomPercentToMm(float pct) const;
```

Signals:
```cpp
void connected(QString portName);
void disconnected();
void errorOccurred(QString message);
void fizStateChanged(FizState state);
void motorCalibrated(uint8_t motorId);
```

### 6.3 `src/application/fizservice.h/.cpp`

Application-layer; sits between UI and NucleusService. Owns the interpolation engine.

```cpp
class FizService : public QObject {
public:
    void initialize(NucleusService* nucleus);

    // Teaching
    void     setFocus(float percent);
    void     setIris(float percent);
    void     setZoom(float percent);
    FizState currentState() const;
    FizState captureCurrentFiz() const;

    // Keyframe management
    void               addKeyframe(const FizKeyframe& kf);
    void               updateKeyframe(const FizKeyframe& kf);
    void               removeKeyframe(const QString& id);
    void               clearKeyframes();
    QList<FizKeyframe> keyframes() const;

    // Playback (called by PlaybackService at 30 Hz)
    FizState interpolateAt(double timeSec) const;
    void     sendInterpolatedFrame(double timeSec);

    // Easing math
    static double applyEasing(double t, FizKeyframe::Easing e);
    static float  lerp(float a, float b, double t);

signals:
    void fizStateChanged(FizState state);
    void keyframesChanged();
};
```

**Interpolation algorithm** (inside `interpolateAt`):
```
1. Find KF_A = last keyframe where kf.time <= T
2. Find KF_B = first keyframe where kf.time > T
3. No KF_A → return first keyframe state (hold)
4. No KF_B → return last keyframe state (hold)
5. alpha = (T - KF_A.time) / (KF_B.time - KF_A.time)
6. eased = applyEasing(alpha, KF_A.easing)
   Linear:    t
   EaseIn:    t*t
   EaseOut:   t*(2.0-t)
   EaseInOut: t<0.5 ? 2*t*t : -1+(4-2*t)*t
7. focus = lerp(KF_A.focus, KF_B.focus, eased)
   iris  = lerp(KF_A.iris,  KF_B.iris,  eased)
   zoom  = lerp(KF_A.zoom,  KF_B.zoom,  eased)
```

### 6.4 `src/presentation/widgets/fizpanel.h/.cpp`

Lives in the Teach Panel area. Manual slider control during teaching.

```
┌──────────────────────────────────────────────┐
│  FIZ LENS CONTROL                            │
│  Port: [COM3 ▼]  [Connect] ● Connected      │
│                                              │
│  ⚠ Confirm adapter is 3.3V (checkbox once)  │
│                                              │
│  FOCUS  ████████████░░░░░  45.2%  1.2m      │
│         [──────────────────────] QSlider     │
│                                              │
│  IRIS   ██████░░░░░░░░░░░  22.0%            │
│         [──────────────────────] QSlider     │
│                                              │
│  ZOOM   ████████████████░  65.0%  58mm      │
│         [──────────────────────] QSlider     │
│                                              │
│  [Calibrate Focus] [Calibrate Iris]         │
│  [Calibrate Zoom]  [Calibrate All]          │
│  [Configure Lens Mapping...]                 │
│  [● Record FIZ with Point]                  │
└──────────────────────────────────────────────┘
```

Slider behavior: `valueChanged` (live, NOT `sliderReleased`) → calls FizService.

Signals: `connectRequested(QString)`, `disconnectRequested()`,  
`recordFizRequested()`, `calibrateRequested(uint8_t)`, `lensMappingRequested()`

Slots: `onFizStateChanged(FizState)`, `onConnectionChanged(bool)`

### 6.5 `src/presentation/widgets/fiztrackwidget.h/.cpp`

Three keyframe tracks rendered below the robot timeline tracks inside `TimelineEditor`.

```
┌─────────────────────────────────────────────────────┐
│ FOCUS  ◆━━━━━━━━━━━━━━━◆━━━━━━━━━━━━━━━━━━━━━━━◆   │
│        0%             45%                      20%  │
│ IRIS   ◆━━━━━━━━━━━━━━━━━━━━━━━━━━━◆━━━━━━━━━━◆   │
│ ZOOM   ◆━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━◆   │
└─────────────────────────────────────────────────────┘
```

- ◆ = `FizKeyframeItem` (QGraphicsEllipseItem subclass)  
  - Click → select, show value in properties  
  - Drag horizontal → change time  
  - Drag vertical → change value 0–100%  
  - Double-click → `FizKeyframeDialog`  
  - Right-click → context menu (delete / duplicate / change easing)  
- Curve between keyframes: `QPainterPath`; straight=Linear, curved=others  
- Track colors: Focus `#9944ff` | Iris `#44aa44` | Zoom `#4488ff`  
- Track height: 60 px each  
- Time axis synced with robot timeline (shared zoom / scroll)

Signals: `keyframeSelected(FizKeyframe)`, `keyframesMoved(QList<FizKeyframe>)`,  
`addKeyframeRequested(int track, double time, float value)`

Slots: `setKeyframes(QList<FizKeyframe>)`, `setPlayheadTime(double)`, `setTimeScale(double)`

### 6.6 `src/presentation/dialogs/lensmappingdialog.h/.cpp`

```
┌────────────────────────────────────┐
│  Lens Mapping Configuration        │
│  Lens Name: [Canon 24-85mm f/3.5] │
│  Focus: Near [300] mm  Far [5000]  │
│  Zoom:  Wide [24] mm   Tele [85]   │
│  [Save] [Cancel]                   │
└────────────────────────────────────┘
```

### 6.7 `src/presentation/dialogs/fizkeyframedialog.h/.cpp`

```
┌──────────────────────────────────┐
│  Edit FIZ Keyframe               │
│  Time:   [3.50] s                │
│  Focus:  [45.2] %  = 1.2m       │
│  Iris:   [22.0] %                │
│  Zoom:   [65.0] %  = 58mm       │
│  Easing: [EaseInOut ▼]          │
│  [Update] [Delete] [Cancel]      │
└──────────────────────────────────┘
```

### 6.8 `src/presentation/dialogs/fizsetupwizard.h/.cpp`

`QWizard`, 6 pages:

| Page | Content |
|---|---|
| 1 — Voltage | Confirm 3.3 V jumper; checkbox required to proceed |
| 2 — Motor Numbers | Step-by-step: set Motor No. 1/2/3 on each physical motor |
| 3 — Wiring | Daisy-chain diagram; custom LEMO cable wire colours |
| 4 — Test | Port selector; [Test] nudges each motor 10% and back; PASS/FAIL |
| 5 — Calibrate | [Calibrate All]; per-motor progress bar |
| 6 — Done | Summary; [Finish] |

---

## 7. Changes to Existing Files

### CMakeLists.txt
```cmake
find_package(Qt6 REQUIRED COMPONENTS SerialPort)
target_link_libraries(CamBotTimeline PRIVATE Qt6::SerialPort)
```

### src/core/types.h
- Add `FizState`, `FizKeyframe`, `LensMapping` structs
- Add `FizState fizState` field to `CameraPoint`
- Add `QList<FizKeyframe> fizKeyframes` to `Project`
- Add `LensMapping lensMapping` to `Project`

### src/presentation/mainwindow.h/.cpp
- Add `FizPanel` to Teach Panel area (below jog buttons)
- Add `FizTrackWidget` below robot timeline tracks
- Add FIZ port selector to connection toolbar
- Menu: `Tools → Configure Lens Mapping…` | `Tools → Calibrate FIZ Motors…`
- Wire `FizService` signals to `FizPanel` and `FizTrackWidget`

### src/presentation/widgets/statusbarwidget.h/.cpp
- New FIZ row: `"FIZ: F:045.2% │ I:022.0% │ Z:065.0%"`
- If lens mapped: `"FIZ: F:1.2m │ I:f/2.8 │ Z:58mm"`
- Color: dim when disconnected, bright white when connected
- Update rate: 30 Hz during playback, 10 Hz during teaching

### src/application/teachservice.h/.cpp
- Add `FizService* m_fiz`
- `recordPoint()`: capture `FizService::currentState()` into `CameraPoint::fizState`
- `goToPoint()`: send `fizState` to motors via `FizService` alongside robot move

### src/application/playbackservice.h/.cpp
- Add `FizService* m_fiz`
- Add `QTimer* m_fizTimer` (30 Hz = 33 ms)
- On Play: start `m_fizTimer` → each tick calls `m_fiz->sendInterpolatedFrame(currentTime)`
- On Pause: stop `m_fizTimer` (motors hold last position; heartbeat continues)
- On Stop: stop `m_fizTimer`
- On Resume: restart `m_fizTimer`
- On E-STOP: stop `m_fizTimer` immediately (heartbeat continues)

### src/infrastructure/persistence/projectrepository.h/.cpp
New JSON fields in `.crp`:
```json
"fizKeyframes": [
  { "id": "<uuid>", "time": 3.5,
    "focus": 45.2, "iris": 22.0, "zoom": 65.0,
    "easing": "EaseInOut" }
],
"lensMapping": {
  "lensName": "Canon 24-85mm",
  "focusNearMm": 300, "focusFarMm": 5000,
  "zoomWideMm": 24, "zoomTeleMm": 85
}
```
Each point in `"points"` gains:
```json
"fizState": { "focus": 45.2, "iris": 22.0, "zoom": 65.0 }
```
Project version bumped to `"1.2.0"`.

---

## 8. Safety Rules for FIZ

1. **Voltage dialog** — first Connect click shows `QMessageBox` requiring explicit confirmation of 3.3 V; stored in `QSettings` so it appears only once per machine.
2. **Motor detection** — after connect, send heartbeat, wait 2000 ms for any response; warn if no motors detected.
3. **Value clamping** — clamp all `float percent` to `[0.0, 100.0]` before packet construction. Never transmit out-of-range values.
4. **Disconnect during playback** — serial `errorOccurred` → pause playback immediately → warn user → motors hold (heartbeat was last sent).
5. **E-STOP integration** — E-STOP must: (a) send `EmergencyStop(1)` via TCP, (b) stop FIZ 30 Hz timer, (c) keep heartbeat running so motors hold position.
6. **Heartbeat always runs** — heartbeat timer stops **only** on explicit `disconnect()`. It keeps running during pause.

---

## 9. Thread Architecture Addition

```
Existing:
  Main thread    → Qt event loop, all UI widgets
  robotThread    → DobotTcpClient (port 29999)
  feedbackThread → RealtimeFeedbackWorker (port 30004)

Added:
  fizThread      → NucleusService (QSerialPort + heartbeat QTimer)
```

All cross-thread calls via `Qt::QueuedConnection`.  
`FizService` lives on the main thread (application layer, no blocking I/O).  
`NucleusService` moved to `fizThread` via `moveToThread()`.

---

## 10. Updated Source Tree

```
src/
├── core/
│   └── types.h                          ← +FizState, FizKeyframe, LensMapping
├── infrastructure/
│   └── fiz/
│       ├── nucleusprotocol.h            ← NEW: static packet builder
│       ├── nucleusservice.h/.cpp        ← NEW: QSerialPort + heartbeat
├── application/
│   ├── fizservice.h/.cpp                ← NEW: interpolation + keyframe mgmt
│   ├── teachservice.h/.cpp              ← modified: capture fizState on record
│   └── playbackservice.h/.cpp          ← modified: 30 Hz FIZ timer
├── presentation/
│   ├── widgets/
│   │   ├── fizpanel.h/.cpp              ← NEW: manual slider control
│   │   └── fiztrackwidget.h/.cpp        ← NEW: timeline keyframe tracks
│   └── dialogs/
│       ├── lensmappingdialog.h/.cpp     ← NEW
│       ├── fizkeyframedialog.h/.cpp     ← NEW
│       └── fizsetupwizard.h/.cpp        ← NEW: 6-page QWizard
└── persistence/
    └── projectrepository.h/.cpp        ← modified: FIZ JSON fields
tests/
├── test_nucleusprotocol.cpp             ← NEW: packet unit tests
├── test_fizservice.cpp                  ← NEW: interpolation unit tests
└── test_fizintegration.py              ← NEW: Python hardware integration test
```

---

## 11. Test Requirements

### `tests/test_nucleusprotocol.cpp` (GoogleTest)

| Test | Assertion |
|---|---|
| `focusCommand(0.0f)` | byte[3]==0x01, byte[5]==0x00, byte[6]==0x00 |
| `focusCommand(100.0f)` | byte[3]==0x01, byte[5]==0x7F, byte[6]==0xFF |
| `focusCommand(50.0f)` | byte[3]==0x01, byte[5]==0x3F, byte[6]==0xFF |
| `irisCommand(50.0f)` | byte[3]==0x02 |
| `zoomCommand(50.0f)` | byte[3]==0x03 |
| Checksum on known packet | matches expected value |
| `focusCommand(-10.0f)` | clamped → same as 0.0f |
| `focusCommand(110.0f)` | clamped → same as 100.0f |
| `heartbeatPacket()` | matches `3A 96 06 00 02 00 01 61` |

### `tests/test_fizservice.cpp` (GoogleTest)

| Test | Assertion |
|---|---|
| Before first keyframe | returns first KF state (hold) |
| After last keyframe | returns last KF state (hold) |
| Midpoint Linear | result = 0.5 when a=0, b=1, t=0.5 |
| EaseIn t=0.5 | result = 0.25 (t*t) |
| EaseOut t=0.5 | result = 0.75 |
| EaseInOut t=0.5 | result = 0.5 |
| Single keyframe | same value at any time |
| Empty keyframes | returns `FizState{0, 0, 0}` |

### `tests/test_fizintegration.py` (pyserial, against real hardware)

```python
# Run: python test_fizintegration.py --port COM3
# Requires: pip install pyserial
# Tests: heartbeat, focus sweep, iris sweep, zoom sweep, 30Hz all-3
```

---

## 12. Build Order for FIZ Feature

Follow this exact sequence — each step must compile cleanly before the next:

| Step | Action |
|---|---|
| 1 | Add `Qt6::SerialPort` to `CMakeLists.txt`; add FIZ types to `types.h`; verify compile |
| 2 | Write `NucleusProtocol`; run `test_nucleusprotocol` — all must pass |
| 3 | Run `test_fizintegration.py` against real hardware; correct any protocol bytes |
| 4 | Write `NucleusService` + `FizService`; test with Python-verified protocol |
| 5 | Write `FizPanel`; wire to `FizService`; test slider → motor live |
| 6 | Write `FizTrackWidget`; integrate below timeline; test KF add/drag/delete |
| 7 | Update `TeachService` to capture FIZ on record; `GoToPoint` restores FIZ |
| 8 | Update `PlaybackService` with 30 Hz timer; test sync robot + FIZ |
| 9 | Write `FizSetupWizard`, `LensMappingDialog`, `FizKeyframeDialog` |
| 10 | Update `ProjectRepository` save/load; round-trip test |
| 11 | Update `StatusBarWidget`; full integration test |

---

## 13. Important Protocol Notes

- The Nucleus-M protocol is **community reverse-engineered**. Any byte whose meaning is inferred (not confirmed by official documentation) must be marked: `// ASSUMPTION: verify against logic analyser output`
- Motor IDs in `byte[3]` ARE the physical Motor Numbers set on each motor body — direct 1:1 mapping.
- FIZ uses `QSerialPort`, NOT `QTcpSocket`. Never mix these transports.
- All internal FIZ values are `float percent (0.0–100.0)`. Real-world unit display (mm, f-stop) is UI-only conversion — never stored or transmitted as real units.
