# CamBot Axis Board Firmware v2

Multi-axis motion firmware for the CamBot rig. **Host and firmware must match** —
the app checks the protocol version on connect and refuses a mismatch.

## Current revision

**Axis 0 (DC servo) only.** The CL57C stepper axis lands in the next revision.
The protocol is already axis-addressed, so adding it needs no host protocol change.

## Flashing

1. Open `cambot_axis_v2.ino` in the Arduino IDE.
2. Board: **Arduino Uno**. Select the right port.
3. Upload.
4. Open Serial Monitor at **115200**, line ending **Newline**. You should see:
   ```
   # CamBot axis board v2 ready
   ```
5. Type `V` and press enter. Expect:
   ```
   V FW=2 PROTO=2 BOARD=UNO AXES=1 CAPS=DC
   ```

If you see nothing, check the baud — v1 ran at 9600 and the app has moved to
115200. Both halves must change together.

## Rollback

The v1 sketch is still at `ardiuno code` in the repo root. Flash it back and
check out an earlier build of the app if you need the old pairing on a shoot.

## Wiring (unchanged from v1)

| Function | Pin |
|---|---|
| DC motor PWM | 5 |
| DC motor DIR | 4 |
| Encoder A | 2 (INT0) |
| Encoder B | 3 (INT1) |
| Home limit switch | 8 (to GND, internal pullup) |

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
| `Q <ax>` | query position (encoder counts) |
| `Z <ax>` | zero the position counter |
| `H <ax>` | query home switch (1 = pressed) |
| `S <ax>` | query status word |
| `R <ax>` | clear a latched Halted state |
| `P` | ping |
| `X` | global stop (does **not** latch Halted) |

### Board → host

```
V FW=2 PROTO=2 BOARD=UNO AXES=1 CAPS=DC
A <ax> <kind>
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

## Watchdog

If no `G` arrives for **500 ms** while the motor is driving, PWM is zeroed and
`! 0 2 watchdog` is emitted once. This is why the host re-sends the PWM every
20 ms tick rather than only on change.

Note that only `G` refreshes it — polling `Q` or `S` deliberately does not, so a
host that has stopped commanding motion cannot keep a stale drive alive.

## Known behaviour worth being aware of

**Opening the port resets the board.** `QSerialPort::open()` asserts DTR, which
reboots the Uno; the sketch is not running for roughly 1.6 s while the
bootloader runs. The host retries the `V` handshake for up to 3 s to cover this.
If you are driving the board from a serial terminal, wait a couple of seconds
after connecting before expecting a reply.

**The encoder ISR still uses `digitalRead`.** That costs ~12 µs per edge and about
16% of the CPU at 300 RPM. It is left as-is in this revision so the protocol
change has exactly one suspect if the DC axis misbehaves. The port-read
optimisation lands with the stepper revision, where the headroom is actually
needed.
