# CamBot Axis Board Firmware v2

Multi-axis motion firmware for the CamBot rig. **Host and firmware must match** —
the app checks the protocol version on connect and refuses a mismatch.

## Current revision

**FW 3 — axis 0 (DC servo) + axis 1 (CL57C closed-loop stepper).**

The protocol is unchanged from FW 2 (`PROTO=2`), because it was already
axis-addressed. Only `FW=` and the capability list moved.

## Flashing

1. Open `cambot_axis_v2.ino` in the Arduino IDE.
2. Board: **Arduino Uno**. Select the right port.
3. Upload.
4. Open Serial Monitor at **115200**, line ending **Newline**. You should see:
   ```
   # CamBot axis board v2 (FW3, DC + STEP) ready
   ```
5. Type `V` and press enter. Expect:
   ```
   V FW=3 PROTO=2 BOARD=UNO AXES=2 CAPS=DC,STEP
   ```

If you see nothing, check the baud — v1 ran at 9600 and the app has moved to
115200. Both halves must change together.

## Rollback

The v1 sketch is still at `ardiuno code` in the repo root. Flash it back and
check out an earlier build of the app if you need the old pairing on a shoot.

## Wiring

### Axis 0 — DC servo (unchanged from v1)

| Function | Pin |
|---|---|
| DC motor PWM | 5 |
| DC motor DIR | 4 |
| Encoder A | 2 (INT0) |
| Encoder B | 3 (INT1) |
| Home limit switch | 8 (to GND, internal pullup) |

### Axis 1 — CL57C stepper

| Function | Pin | Notes |
|---|---|---|
| STEP | **9** (PB1) | Timer1 CTC, OC1A disconnected — plain GPIO |
| DIR | 7 (PD7) | |
| ENABLE | 6 (PD6) | torque switch, **not** an e-stop |
| ALM in | A0 | `INPUT_PULLUP`, polled and debounced |
| Home switch | A1 | optional; `STEP_HAS_HOME` is `false` by default |

> **Bond Arduino GND to the CL57C signal ground.** Without it the opto inputs
> float and the drive either misses steps or ignores STEP entirely — and it does
> so intermittently, which is the worst way to find out.

**Why STEP is on pin 9 specifically.** The step ISR touches `PORTB` and nothing
else; the main loop writes `PORTD` (DC DIR on PD4, stepper DIR on PD7, ENABLE on
PD6). Keeping them on different ports means the ISR can never interrupt a
loop-side read-modify-write of a register it is about to write. That race would
glitch a direction line roughly once an hour and cost a day to find. **Do not
move STEP to a PORTD pin.**

## CL57C DIP settings

**Set pulses/rev to 1600.**

At 1600 p/r, the design ceiling of 8 kHz is 5 rev/s = 300 RPM, matching the DC
axis. Resolution stays fine: 3.1 µm/step on a 5 mm screw, 25 µm/step on a 20T
GT2 belt. Use 800 if you need more speed. **Do not use 3200 or above on an
Uno** — see the budget below.

Set the current one notch below the motor's rated value. A closed-loop drive
only draws what it needs, and the headroom keeps the driver cool.

## CPU budget — why 1600, and why the encoder ISR changed

At 2709 counts/rev and 300 RPM the encoder fires **13,546 times a second**. The
v1/FW2 ISR used two `digitalRead`s and cost ~12 µs an edge: **16% of the CPU
gone before anything else ran.** FW 3 replaces it with raw `INT0`/`INT1` vectors,
one `PIND` read and a 16-byte transition table — about 3 µs, ~4% CPU. That is
precisely the headroom the step ISR spends.

| Step rate | Step ISR | + optimised encoder | Verdict |
|---|---|---|---|
| 8 kHz | 4% | 8% | **design target** |
| 12 kHz | 6% | 10% | works, jitter appears |
| 20 kHz | 10% | 14% | board ceiling (`STEP_RATE_CEILING`) |
| 40 kHz | 20% | 24% | not viable |

Interrupt *latency* bites before CPU percentage does: two encoder ISRs plus a
step ISR can serialise into ~20 µs blocking windows. **One stepper plus one DC
axis is the Uno's realistic ceiling.** More axes needs a Mega2560 — Timer1/3/4/5
give four step axes and INT0–5 give three encoders.

## Signal polarity

CL57C inputs are opto-isolated and usually wired common-anode, so pulling the
pin **low** turns the opto on. These constants are at the top of the sketch:

| Constant | Default | Verified by |
|---|---|---|
| `STEP_ACTIVE_LOW` | `true` | step 3 of the checklist |
| `DIR_INVERT` | `false` | step 3 |
| `ENABLE_ACTIVE_LOW` | `true` | step 2 |
| `ALARM_ACTIVE_LOW` | `true` | step 9 |

**If the axis runs the wrong way, flip `DIR_INVERT` here — do not negate
`stepsPerUnit` on the host.** A negative scale factor silently inverts homing
and the travel-limit clamp, so the safety net ends up protecting the wrong end
of the axis.

## Protocol

Newline-terminated ASCII at 115200. Every command names an axis; every reply
leads with a type character so the host parses by content, never by
"whatever I asked last".

### Host → board

| Command | Meaning |
|---|---|
| `V` | version / capability |
| `A` | enumerate axes |
| `G <ax> <pwm>` | DC drive, signed −255..255. **Kicks the watchdog.** |
| `T <ax> <steps>` | stepper absolute target, in steps. **Kicks the watchdog.** |
| `J <ax> <rate>` | stepper jog, signed steps/s. **Kicks the watchdog.** |
| `L <ax> <vmax> <amax>` | stepper clamps, steps/s and steps/s² |
| `E <ax> <0\|1>` | stepper ENABLE (torque) |
| `Q <ax>` | query position (encoder counts, or steps) |
| `Z <ax>` | zero the position counter |
| `H <ax>` | query home switch (1 = pressed) |
| `S <ax>` | query status word |
| `R <ax>` | clear a latched Halted state |
| `P` | ping |
| `X` | global stop (does **not** latch Halted, does **not** drop ENABLE) |

Commands are axis-typed: `G` on axis 1 or `T` on axis 0 is a `bad command`
fault, not a silent no-op.

### Board → host

```
V FW=3 PROTO=2 BOARD=UNO AXES=2 CAPS=DC,STEP
A <ax> <kind>            DC | STEP
Q <ax> <counts>
H <ax> <0|1>
S <ax> <flagsHex> <position> <rate>
P OK
! <ax> <code> <text>     asynchronous fault — may arrive at any time
# <text>                 comment; the host ignores these
```

Status flags: `01` enabled · `02` moving · `04` home switch · `08` alarm ·
`10` halted · `20` position lost · `40` watchdog tripped

Fault codes: `1` alarm · `2` watchdog · `3` limit · `4` bad command · `5` over rate

## Motion model — the host owns the trajectory

The host streams an **absolute step target every 20 ms** and Timer1 steps toward
the latest one, clamped to `vmax`/`amax`. The board does not generate its own
trajectory.

This is deliberate. If the firmware self-profiled, the axis would arrive when
*it* decided rather than when the keyframe said — and multi-axis sync and
take-to-take repeatability are exactly that decision. It also fails better: a
serial stall under firmware-side trajectory leaves a long queued move running
blind, whereas here it simply means no fresh setpoint, and the watchdog ramps
the axis down within 500 ms.

The cost is 20 ms setpoint granularity, a velocity staircase of 160 steps per
window at 8 kHz. That is mechanically invisible, and the `amax` clamp plus the
drive's own loop smooth it further. If a future rig needs better, raise the host
tick to 100 Hz — do not move the profile into firmware.

## Watchdogs

**Axis 0.** If no `G` arrives for **500 ms** while the motor is driving, PWM is
zeroed and `! 0 2 …` is emitted once.

**Axis 1.** Watches the **setpoint stream** — `T` and `J` — specifically, not
"any command". A host that keeps polling `Q` while its playback thread has died
must not be able to keep a stale target alive. On expiry the axis **ramps down
at `amax`** rather than stopping dead: an abrupt stop at speed is how a
closed-loop drive loses sync and how the rig gets jerked. If it is somehow still
moving 250 ms later it stops dead anyway. Either way `Halted` latches, and
recovery needs an explicit `R` — which also adopts the current position as the
target, so a reconnect can never resume a move the operator has lost track of.

This is why the host re-sends the setpoint every tick rather than only on change.

## ENABLE is a torque switch, not an e-stop

De-asserting ENABLE removes holding torque, and a gravity-loaded axis then
**falls**. Stopping means decelerate and hold, and holding needs the drive
energised — so neither `X` nor any watchdog or alarm path ever drops ENABLE.
`E <ax> 0` is only ever a deliberate "release the axis" action, and the host puts
a confirmation naming the drop risk in front of it.

## The alarm line is not optional

The CL57C closes its own loop, so the host's idea of the stepper's position is
**what it asked for, not what happened**. A missed step is invisible from the
host side. ALM is the only integrity signal this axis has.

It is polled every pass and debounced for 20 ms (a run next to a stepper's phase
wires picks up plenty of noise). On assertion the axis stops dead — no graceful
ramp, because the drive has already faulted and the count is already wrong —
latches `Halted` and `PositionLost`, and emits `! 1 1 drive alarm`. `R` is
refused while ALM is still asserted.

**Wire it.** Without it the rig will happily keep shooting against a step count
that no longer describes where the camera is.

## Known behaviour worth being aware of

**Opening the port resets the board.** `QSerialPort::open()` asserts DTR, which
reboots the Uno; the sketch is not running for roughly 1.6 s while the
bootloader runs. The host retries the `V` handshake for up to 3 s to cover this.
If you are driving the board from a serial terminal, wait a couple of seconds
after connecting before expecting a reply.

**The stepper's minimum rate is ~32 steps/s.** Timer1 runs at 2 MHz with a
16-bit compare register, so slower than that is not expressible; the follower
treats anything below it as stopped. At 1600 p/r that is 0.02 rev/s, which is
below anything useful anyway.

**An idle stepper costs zero interrupts.** `OCIE1A` is cleared when the axis
reaches its target, so the encoder gets the whole board back whenever the
stepper is parked.

## Bench checklist

Run this from a serial terminal before any app-side stepper code exists. With
the motor **uncoupled from the rig** for steps 2–3.

1. **Handshake** — open at 115200, wait ≥2 s for the reset, send `V`. No reply
   means wrong port, wrong baud, or v1 still flashed.
2. **Enable** — `E 1 1`; the shaft should resist by hand. `E 1 0` frees it.
   Backwards ⇒ flip `ENABLE_ACTIVE_LOW`.
3. **Direction** — `E 1 1`, then `J 1 200` for a second, then `J 1 0`. It should
   turn positive and `Q 1` should read ≈ 200. Wrong way ⇒ flip `DIR_INVERT`.
4. **Steps per unit** — couple to the rig, `Z 1`, `T 1 16000` (10 rev), measure
   the actual travel. Cross-check against `pulsesPerRev × gearRatio / mmPerRev`.
   **More than 2% disagreement means a wrong DIP or a slipping belt — stop and
   find it**, do not calibrate around it.
5. **Repeatability** — five 0 → 16000 → 0 cycles with a dial indicator on the
   return. Drift is lost steps.
6. **Pulse ceiling** — jog at 2k, 4k, 8k, 12k, 16k, 20k Hz, comparing `Q`
   against physical position each time. **Then repeat with the DC axis jogging
   at full speed simultaneously** and note the lower divergence point. Watch the
   app log for `"Encoder query timed out"` — that is the built-in ISR-contention
   canary. Set the host's `stepRateCeilingHz` to 70% of the lowest rate found.
7. **ENABLE under load** — at the rig's worst gravity-loaded orientation, send
   `E 1 0` and watch, hand ready. If it drops, `idleDisable` stays off
   permanently.
8. **Watchdog** — mid-move, **physically unplug the USB**. It must ramp down and
   stop within ~500 ms, not continue to target. Reconnect: `S 1` shows halted,
   `T` is ignored, `R` is required. Repeat by killing the app from Task Manager.
9. **Alarm** — turn the drive current down, stall the shaft until ALM asserts.
   Expect `! 1 1 drive alarm`, stepping halted, `R` refused until it clears.
10. **Combined** — once the host side exists, DC and stepper both moving on one
    timeline, three runs, comparing final `Q` against commanded each time.
