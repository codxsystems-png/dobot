/***************************************************************
 * CamBotTimeline — Axis Board Firmware v3  (FW 4, PROTO 3)
 *
 * THREE step/dir axes. No DC servo, no encoder, no PID.
 *
 * The rig uses closed-loop stepper drives (CL57C and similar):
 * the drive closes its own loop against its internal encoder, so
 * the board's only job is to clock accurate step pulses and count
 * them. That removes the entire PID/encoder half of the previous
 * firmware, and with it the 13.5k interrupts/sec the quadrature
 * decoder used to cost.
 *
 * ─── One timer for all three axes ────────────────────────────
 * An Uno has one usable 16-bit timer, so an axis cannot have one
 * each. Instead a single ISR runs at a fixed STEP_ISR_HZ and each
 * axis carries a 32-bit phase accumulator: every tick the axis
 * adds its increment, and a carry out of 32 bits emits one step.
 * Rate is therefore set by the increment, not by the timer, and
 * the maximum rate per axis is exactly STEP_ISR_HZ.
 *
 * This is the standard DDA approach and it has three properties
 * that matter here: the cost per tick is the same whether one
 * axis moves or three, the axes stay in phase with each other
 * (which is what multi-axis sync means), and the same code runs
 * unchanged on a Mega with more axes — only the pin table grows.
 *
 * ─── Pulse width comes free ──────────────────────────────────
 * Step pins are driven active at the END of one ISR and released
 * at the START of the next, so the pulse is one full ISR period
 * (50us at 20kHz) and costs no delay loop at all. Holding the
 * pin with _delay_us inside the ISR would burn 3us of every tick
 * — 6% of the CPU at 20kHz, for nothing.
 *
 * ─── Wiring ──────────────────────────────────────────────────
 *   Axis 0: STEP = 9  (PB1)   DIR = 4 (PD4)   ALM = A0
 *   Axis 1: STEP = 10 (PB2)   DIR = 5 (PD5)   ALM = A1
 *   Axis 2: STEP = 11 (PB3)   DIR = 6 (PD6)   ALM = A2
 *
 *   Every STEP is on PORTB and every DIR on PORTD, deliberately.
 *   The ISR writes only PORTB and the main loop writes only
 *   PORTD, so the ISR can never interrupt a read-modify-write of
 *   a register it is about to touch. That race glitches a
 *   direction line about once an hour and costs a day to find.
 *
 *   *** Bond Arduino GND to each drive's signal ground. ***
 *
 * ─── ENABLE is deliberately not driven ───────────────────────
 * On this rig's drives the ENA input means DISABLE: energising
 * it switches the drive off. Driving that pin killed the motor
 * in BOTH polarities while the drive sat green and unalarmed,
 * because it was doing exactly what it was told. Left open, the
 * drive is live whenever powered, which is what we want. See
 * STEP_HAS_ENABLE before wiring one.
 *
 * Serial: 115200 baud, newline-terminated.
 *
 * Host -> board:
 *   V                     version / capability
 *   A                     enumerate axes
 *   T <ax> <steps>        absolute target.  Kicks the watchdog.
 *   J <ax> <rate>         jog, signed steps/s.  Kicks it too.
 *   L <ax> <vmax> <amax>  clamps, steps/s and steps/s^2
 *   E <ax> <0|1>          enable (no-op unless ENA is wired)
 *   Q <ax>                query position, in steps
 *   Z <ax>                zero the step counter
 *   H <ax>                query home switch
 *   S <ax>                status word
 *   R <ax>                clear a latched halt
 *   D <ax>                commissioning diagnostic, raw pins
 *   W <ax>                exercise a pin at 1Hz so a meter sees it
 *   P                     ping
 *   X                     global stop (decelerates, holds torque)
 *
 * Board -> host:
 *   V FW=4 PROTO=3 BOARD=UNO AXES=3 CAPS=STEP
 *   A <ax> STEP
 *   Q <ax> <steps>
 *   H <ax> <0|1>
 *   S <ax> <flagsHex> <pos> <rate>
 *   P OK
 *   ! <ax> <code> <text>  asynchronous fault, any time
 *   # <text>              comment; the host ignores these
 ***************************************************************/

#define FW_VERSION     4
#define PROTO_VERSION  3
#define BOARD_NAME     "UNO"

// ─── Axes ─────────────────────────────────────────────────────
// Raising this on a Mega needs only a longer pin table, provided
// every STEP stays on one port and every DIR on another.
const uint8_t AXIS_COUNT = 3;

const uint8_t PIN_STEP[AXIS_COUNT] = {  9, 10, 11 };   // PB1, PB2, PB3
const uint8_t PIN_DIR [AXIS_COUNT] = {  4,  5,  6 };   // PD4, PD5, PD6
const uint8_t PIN_ALM [AXIS_COUNT] = { A0, A1, A2 };
const uint8_t PIN_HOME[AXIS_COUNT] = { A3, A4, A5 };
// Only claimed when STEP_HAS_ENABLE is true; left as inputs otherwise so
// they stay free on a board that does not wire ENA.
const uint8_t PIN_ENABLE[AXIS_COUNT] = { 7, 8, 12 };

// Step bits within PORTB, in axis order. Kept as a mask so the
// ISR can pulse any combination of axes with a single write.
const uint8_t STEP_BIT[AXIS_COUNT]  = { _BV(PB1), _BV(PB2), _BV(PB3) };
const uint8_t STEP_MASK = _BV(PB1) | _BV(PB2) | _BV(PB3);

// ─── Signal polarity ──────────────────────────────────────────
// Verified by the bench checklist. If an axis runs the wrong way
// flip DIR_INVERT here rather than negating stepsPerUnit on the
// host — a negative scale factor silently inverts the travel
// clamp, so the safety net ends up guarding the wrong end.
const bool STEP_ACTIVE_LOW = true;
const bool DIR_INVERT[AXIS_COUNT] = { false, false, false };
const bool ALARM_ACTIVE_LOW = true;

// This rig has no ENA connection and no home switches; both
// commands still answer so the protocol stays uniform.
const bool STEP_HAS_ENABLE = false;
const bool HAS_HOME_SWITCH[AXIS_COUNT] = { false, false, false };

// ─── Step generation ──────────────────────────────────────────
// The ISR rate is also the maximum step rate per axis: one carry
// per tick is one step. 20kHz matches the ceiling the previous
// firmware reached, and leaves the CPU mostly idle now that the
// encoder decoder is gone.
const uint32_t STEP_ISR_HZ = 20000UL;
const uint32_t TIMER1_HZ   = 2000000UL;          // prescaler 8

// increment = rate * 2^32 / STEP_ISR_HZ, precomputed as a scale.
// At rate == STEP_ISR_HZ this is exactly 2^32, i.e. a carry every
// tick, which is the ceiling.
const uint32_t PHASE_SCALE = (uint32_t)(4294967296.0 / (double)STEP_ISR_HZ);

// ─── Status flags (must match axisproto::StatusFlag) ──────────
const uint8_t ST_ENABLED       = 0x01;
const uint8_t ST_MOVING        = 0x02;
const uint8_t ST_HOME_SWITCH   = 0x04;
const uint8_t ST_ALARM         = 0x08;
const uint8_t ST_HALTED        = 0x10;
const uint8_t ST_POSITION_LOST = 0x20;
const uint8_t ST_WATCHDOG      = 0x40;

// ─── Fault codes (must match axisproto::FaultCode) ────────────
const uint8_t FAULT_ALARM    = 1;
const uint8_t FAULT_WATCHDOG = 2;
const uint8_t FAULT_LIMIT    = 3;
const uint8_t FAULT_BADCMD   = 4;
const uint8_t FAULT_OVERRATE = 5;

const unsigned long MOTOR_WATCHDOG_MS         = 500;
const unsigned long WATCHDOG_HARDSTOP_MS      = 250;

// ─── Per-axis state ───────────────────────────────────────────
// Touched by the ISR; every multi-byte read from loop() must be
// wrapped in noInterrupts().
volatile long     stepPos[AXIS_COUNT]    = { 0, 0, 0 };
volatile long     isrRemaining[AXIS_COUNT] = { 0, 0, 0 };
volatile bool     isrContinuous[AXIS_COUNT] = { false, false, false };
volatile int8_t   isrDelta[AXIS_COUNT]   = { 1, 1, 1 };
volatile uint32_t phaseAcc[AXIS_COUNT]   = { 0, 0, 0 };
volatile uint32_t phaseInc[AXIS_COUNT]   = { 0, 0, 0 };   // 0 = idle

// Owned by loop() alone.
long  stepTarget[AXIS_COUNT] = { 0, 0, 0 };
long  jogRate[AXIS_COUNT]    = { 0, 0, 0 };
bool  jogging[AXIS_COUNT]    = { false, false, false };
bool  decelToStop[AXIS_COUNT]= { false, false, false };
float rate[AXIS_COUNT]       = { 0, 0, 0 };               // signed, steps/s
long  vmax[AXIS_COUNT]       = { 8000, 8000, 8000 };
long  amax[AXIS_COUNT]       = { 40000, 40000, 40000 };

bool  axEnabled[AXIS_COUNT]  = { false, false, false };
bool  axHalted[AXIS_COUNT]   = { false, false, false };
bool  axAlarm[AXIS_COUNT]    = { false, false, false };
bool  axWatchdog[AXIS_COUNT] = { false, false, false };

unsigned long lastSetpointMs[AXIS_COUNT] = { 0, 0, 0 };
unsigned long watchdogAtMs[AXIS_COUNT]   = { 0, 0, 0 };
unsigned long alarmStableMs[AXIS_COUNT]  = { 0, 0, 0 };
bool          alarmRawLast[AXIS_COUNT]   = { false, false, false };

unsigned long followerLastUs = 0;
const unsigned long FOLLOWER_PERIOD_US = 2000;

// Below this the DDA increment rounds toward zero and motion
// becomes lumpy rather than slow, so treat it as stopped.
const float RATE_MIN = 1.0f;

// ─── Command buffer ───────────────────────────────────────────
// Fixed, not String: 2KB of SRAM, and String concatenation at
// 115200 fragments the heap into an eventual hang mid-take.
const uint8_t CMD_BUF_LEN = 40;
char    cmdBuf[CMD_BUF_LEN];
uint8_t cmdLen = 0;

void replyFault(uint8_t axis, uint8_t code, const char* text);
bool axisMoving(uint8_t ax);
void hardStop(uint8_t ax);

// ─── Step ISR ─────────────────────────────────────────────────
// Fixed rate, all axes. Contains no division, no float, no
// digitalWrite and no millis(): it runs 20000 times a second and
// everything in here is multiplied by that.
ISR(TIMER1_COMPA_vect)
{
  // Release the previous tick's pulses. One write, all axes.
  if (STEP_ACTIVE_LOW) PORTB |=  STEP_MASK;
  else                 PORTB &= ~STEP_MASK;

  uint8_t pulse = 0;

  for (uint8_t a = 0; a < AXIS_COUNT; a++) {
    if (phaseInc[a] == 0) continue;
    if (!isrContinuous[a] && isrRemaining[a] == 0) continue;

    const uint32_t prev = phaseAcc[a];
    phaseAcc[a] = prev + phaseInc[a];
    if (phaseAcc[a] >= prev) continue;      // no carry, no step this tick

    pulse   |= STEP_BIT[a];
    stepPos[a] += isrDelta[a];
    if (!isrContinuous[a] && isrRemaining[a] > 0) isrRemaining[a]--;
  }

  if (pulse) {
    // Assert the pulses; they are released at the top of the next
    // tick, which gives a full ISR period of pulse width for free.
    if (STEP_ACTIVE_LOW) PORTB &= ~pulse;
    else                 PORTB |=  pulse;
  }
}

// ─── Helpers ──────────────────────────────────────────────────
bool axisValid(long ax) { return ax >= 0 && ax < AXIS_COUNT; }

bool axisMoving(uint8_t ax)
{
  noInterrupts();
  const bool m = (phaseInc[ax] != 0);
  interrupts();
  return m;
}

bool anyAxisMoving()
{
  for (uint8_t a = 0; a < AXIS_COUNT; a++) if (axisMoving(a)) return true;
  return false;
}

void setDirection(uint8_t ax, bool positive)
{
  const bool level = DIR_INVERT[ax] ? !positive : positive;
  digitalWrite(PIN_DIR[ax], level ? HIGH : LOW);
  noInterrupts();
  isrDelta[ax] = positive ? 1 : -1;
  interrupts();
  delayMicroseconds(10);      // DIR must settle before the next STEP edge
}

void setEnable(uint8_t ax, bool on)
{
  if (!STEP_HAS_ENABLE) {
    // No ENA wired: the drive is live whenever powered. Report
    // that honestly — the follower gates on axEnabled, so
    // claiming "disabled" would silently refuse all motion.
    axEnabled[ax] = true;
    return;
  }
  axEnabled[ax] = on;
  digitalWrite(PIN_ENABLE[ax], on ? HIGH : LOW);
}

void applyRate(uint8_t ax, float signedRate)
{
  const float mag = fabs(signedRate);
  noInterrupts();
  if (mag < RATE_MIN) {
    phaseInc[ax] = 0;
    phaseAcc[ax] = 0;
  } else {
    uint32_t inc = (uint32_t)(mag * (float)PHASE_SCALE);
    if (inc == 0) inc = 1;
    phaseInc[ax] = inc;
  }
  interrupts();

  // The timer only runs while something is moving, so an idle
  // board costs zero interrupts.
  if (anyAxisMoving()) TIMSK1 |=  _BV(OCIE1A);
  else                 TIMSK1 &= ~_BV(OCIE1A);
}

void hardStop(uint8_t ax)
{
  noInterrupts();
  phaseInc[ax]      = 0;
  phaseAcc[ax]      = 0;
  isrRemaining[ax]  = 0;
  isrContinuous[ax] = false;
  stepTarget[ax]    = stepPos[ax];
  interrupts();

  rate[ax]        = 0.0f;
  jogging[ax]     = false;
  jogRate[ax]     = 0;
  decelToStop[ax] = false;

  if (!anyAxisMoving()) TIMSK1 &= ~_BV(OCIE1A);
}

void replyFault(uint8_t axis, uint8_t code, const char* text)
{
  Serial.print('!');  Serial.print(' ');
  Serial.print(axis); Serial.print(' ');
  Serial.print(code); Serial.print(' ');
  Serial.println(text);
}

long axisPosition(uint8_t ax)
{
  noInterrupts();
  const long v = stepPos[ax];
  interrupts();
  return v;
}

uint8_t statusFlags(uint8_t ax)
{
  uint8_t f = 0;
  if (axEnabled[ax])  f |= ST_ENABLED;
  if (axisMoving(ax)) f |= ST_MOVING;
  if (axAlarm[ax])    f |= ST_ALARM | ST_POSITION_LOST;
  if (axHalted[ax])   f |= ST_HALTED;
  if (axWatchdog[ax]) f |= ST_WATCHDOG;
  if (HAS_HOME_SWITCH[ax] && digitalRead(PIN_HOME[ax]) == LOW) f |= ST_HOME_SWITCH;
  return f;
}

// ─── Follower ─────────────────────────────────────────────────
// Turns each axis's target into a rate, honouring vmax and amax.
// The TRAJECTORY belongs to the host: this only clamps. Firmware
// -side profiling would mean an axis arrives when the board
// decides rather than when the keyframe says, and multi-axis sync
// is exactly that decision.
void serviceAxes()
{
  const unsigned long nowUs = micros();
  if ((unsigned long)(nowUs - followerLastUs) < FOLLOWER_PERIOD_US) return;
  const float dt = (float)(unsigned long)(nowUs - followerLastUs) / 1000000.0f;
  followerLastUs = nowUs;

  for (uint8_t a = 0; a < AXIS_COUNT; a++) {
    if (axHalted[a] || axAlarm[a] || !axEnabled[a]) {
      if (axisMoving(a)) hardStop(a);
      continue;
    }

    noInterrupts();
    const long pos = stepPos[a];
    interrupts();

    float desired;
    if (decelToStop[a]) {
      desired = 0.0f;
    } else if (jogging[a]) {
      desired = (float)jogRate[a];
    } else {
      const long remaining = stepTarget[a] - pos;
      if (remaining == 0) {
        // Already there. Snap rather than ramp: the vStop clamp
        // brought us in, so there is no speed left to shed, and a
        // lingering rate would keep reporting ST_MOVING while parked.
        rate[a] = 0.0f;
        noInterrupts();
        phaseInc[a] = 0; isrRemaining[a] = 0; isrContinuous[a] = false;
        interrupts();
        if (!anyAxisMoving()) TIMSK1 &= ~_BV(OCIE1A);
        continue;
      }
      // Fastest speed from which amax can still stop in the
      // distance left; without it the axis overshoots every target.
      const float vStop = sqrt(2.0f * (float)amax[a] * (float)labs(remaining));
      float v = (float)vmax[a];
      if (vStop < v) v = vStop;
      desired = (remaining > 0) ? v : -v;
    }

    const float ceiling = (float)min(vmax[a], (long)STEP_ISR_HZ);
    if (desired >  ceiling) desired =  ceiling;
    if (desired < -ceiling) desired = -ceiling;

    // Slew toward it at amax. The floor is applied to what the DDA
    // is PROGRAMMED with, never to the ramp itself — clamping the
    // ramp to zero deadlocks an axis whose first increment is below
    // the floor, which is a real bug this firmware already had once.
    const float maxDelta = (float)amax[a] * dt;
    float next = rate[a];
    if (desired > next)      next = min(desired, next + maxDelta);
    else if (desired < next) next = max(desired, next - maxDelta);

    // A reversal must pass through zero: changing DIR with pulses
    // in flight is how a drive loses count, and the physics agrees.
    if ((next > 0.0f && rate[a] < 0.0f) || (next < 0.0f && rate[a] > 0.0f)) next = 0.0f;
    rate[a] = next;

    if (fabs(rate[a]) < RATE_MIN) {
      rate[a] = 0.0f;
      noInterrupts();
      phaseInc[a] = 0; isrRemaining[a] = 0; isrContinuous[a] = false;
      if (decelToStop[a]) stepTarget[a] = stepPos[a];
      interrupts();
      decelToStop[a] = false;
      if (!anyAxisMoving()) TIMSK1 &= ~_BV(OCIE1A);
      continue;
    }

    const bool positive = rate[a] > 0.0f;
    if ((positive && isrDelta[a] < 0) || (!positive && isrDelta[a] > 0)) {
      setDirection(a, positive);
    }

    // Hand the ISR the exact remaining count so it stops on target
    // rather than waiting for the next follower pass — at 20kHz
    // that would be up to 40 steps of overshoot.
    noInterrupts();
    if (jogging[a]) {
      isrContinuous[a] = true;
      isrRemaining[a]  = 0;
    } else {
      isrContinuous[a] = false;
      const long rem = stepTarget[a] - stepPos[a];
      isrRemaining[a] = (rem >= 0) ? rem : -rem;
    }
    interrupts();

    applyRate(a, rate[a]);
  }
}

// ─── Alarm ────────────────────────────────────────────────────
// The drive closes its own loop, so the host's position is what it
// ASKED for and a missed step is invisible. ALM is the only
// integrity signal an axis has, which is why tripping it clears
// the position rather than merely being reported.
void serviceAlarms()
{
  const unsigned long now = millis();
  for (uint8_t a = 0; a < AXIS_COUNT; a++) {
    const bool raw = (digitalRead(PIN_ALM[a]) == LOW) == ALARM_ACTIVE_LOW;
    if (raw != alarmRawLast[a]) {
      alarmRawLast[a]  = raw;
      alarmStableMs[a] = now;
      continue;
    }
    // 20ms of agreement: a run beside a stepper's phase wires
    // picks up plenty of noise.
    if ((now - alarmStableMs[a]) < 20) continue;

    if (raw && !axAlarm[a]) {
      axAlarm[a]  = true;
      axHalted[a] = true;
      hardStop(a);   // the count is already wrong; do not ramp
      replyFault(a, FAULT_ALARM, "drive alarm");
    } else if (!raw && axAlarm[a]) {
      axAlarm[a] = false;   // clears the condition, NOT the latched halt
    }
  }
}

// ─── Watchdog ─────────────────────────────────────────────────
// Watches the SETPOINT stream (T/J) per axis, not "any command":
// a host that keeps polling Q while its playback thread has died
// must not keep a stale target alive.
void serviceWatchdogs()
{
  const unsigned long now = millis();
  for (uint8_t a = 0; a < AXIS_COUNT; a++) {
    if (!axWatchdog[a]) {
      if (axisMoving(a) && (now - lastSetpointMs[a]) > MOTOR_WATCHDOG_MS) {
        axWatchdog[a]   = true;
        watchdogAtMs[a] = now;
        // Ramp down at amax rather than stopping dead: an abrupt
        // stop at speed is how a closed-loop drive loses sync.
        decelToStop[a] = true;
        jogging[a]     = false;
        jogRate[a]     = 0;
        replyFault(a, FAULT_WATCHDOG, "setpoint stream stopped");
      }
    } else if (axisMoving(a) && (now - watchdogAtMs[a]) > WATCHDOG_HARDSTOP_MS) {
      hardStop(a);          // decel should have finished; accept lost sync
      axHalted[a] = true;
    } else if (!axisMoving(a) && !axHalted[a]) {
      axHalted[a] = true;   // ramp complete; recovery needs an explicit R
    }
  }
}

// ─── Command dispatch ─────────────────────────────────────────
void handleCommand(char* line)
{
  if (line[0] == '\0') return;
  const char type = line[0];

  strtok(line, " ");
  char* a1 = strtok(NULL, " ");
  char* a2 = strtok(NULL, " ");
  char* a3 = strtok(NULL, " ");
  const long ax = a1 ? atol(a1) : -1;

  switch (type) {

    case 'V':
      Serial.print(F("V FW="));   Serial.print(FW_VERSION);
      Serial.print(F(" PROTO=")); Serial.print(PROTO_VERSION);
      Serial.print(F(" BOARD=")); Serial.print(F(BOARD_NAME));
      Serial.print(F(" AXES="));  Serial.print(AXIS_COUNT);
      Serial.println(F(" CAPS=STEP"));
      break;

    case 'A':
      for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        Serial.print(F("A ")); Serial.print(a); Serial.println(F(" STEP"));
      }
      break;

    case 'P': Serial.println(F("P OK")); break;

    case 'X':
      // Operator stop: decelerate and HOLD. Does not latch Halted
      // (this is not a fault) and never drops torque — cutting it
      // on a gravity-loaded axis makes it fall.
      for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        decelToStop[a] = true;
        jogging[a]     = false;
        jogRate[a]     = 0;
      }
      break;

    case 'T': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (!a2)            { replyFault(ax, FAULT_BADCMD, "missing target"); break; }
      if (axHalted[ax] || axAlarm[ax]) break;   // latched: ignore until R
      stepTarget[ax]     = atol(a2);
      jogging[ax]        = false;
      jogRate[ax]        = 0;
      decelToStop[ax]    = false;
      lastSetpointMs[ax] = millis();
      break;
    }

    case 'J': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (!a2)            { replyFault(ax, FAULT_BADCMD, "missing rate"); break; }
      if (axHalted[ax] || axAlarm[ax]) break;
      const long r = atol(a2);
      if (labs(r) > (long)STEP_ISR_HZ) {
        replyFault(ax, FAULT_OVERRATE, "above board step ceiling");
        break;
      }
      jogRate[ax]     = r;
      jogging[ax]     = (r != 0);
      decelToStop[ax] = false;
      if (!jogging[ax]) {
        // Leaving jog: RAMP DOWN, do not adopt a position.
        //
        // Capturing stepPos here looks equivalent but is not: the axis is
        // still moving, so it coasts past the captured point, `remaining`
        // goes negative and the follower REVERSES to go back for it. That
        // is motion with no setpoint stream, so the watchdog trips and
        // latches Halted — after which every further J is silently
        // ignored and the axis never jogs again.
        decelToStop[ax] = true;
      }
      lastSetpointMs[ax] = millis();
      break;
    }

    case 'L': {
      if (!axisValid(ax))  { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (!a2 || !a3)      { replyFault(ax, FAULT_BADCMD, "missing limits"); break; }
      const long v = atol(a2);
      const long acc = atol(a3);
      if (v   > 0) vmax[ax] = min(v, (long)STEP_ISR_HZ);
      if (acc > 0) amax[ax] = acc;
      break;
    }

    case 'E': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (!a2)            { replyFault(ax, FAULT_BADCMD, "missing state"); break; }
      const bool on = (atoi(a2) != 0);
      if (!on) hardStop(ax);            // never drop torque mid-pulse
      setEnable(ax, on);
      break;
    }

    case 'Q': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      Serial.print(F("Q ")); Serial.print(ax);
      Serial.print(' ');     Serial.println(axisPosition(ax));
      break;
    }

    case 'Z': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      // Target moves with the origin, or zeroing commands an
      // immediate run back to the old target's step count.
      noInterrupts();
      stepPos[ax]      = 0;
      isrRemaining[ax] = 0;
      interrupts();
      stepTarget[ax] = 0;
      break;
    }

    case 'H': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      const bool pressed = HAS_HOME_SWITCH[ax] && (digitalRead(PIN_HOME[ax]) == LOW);
      Serial.print(F("H ")); Serial.print(ax);
      Serial.print(' ');     Serial.println(pressed ? 1 : 0);
      break;
    }

    case 'S': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      Serial.print(F("S ")); Serial.print(ax);
      Serial.print(' ');     Serial.print(statusFlags(ax), HEX);
      Serial.print(' ');     Serial.print(axisPosition(ax));
      Serial.print(' ');     Serial.println((long)rate[ax]);
      break;
    }

    case 'R': {
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (axAlarm[ax]) {
        replyFault(ax, FAULT_ALARM, "alarm still asserted");
        break;
      }
      // Adopt the current position, so a resumed link can never
      // continue a move the operator has since lost track of.
      noInterrupts();
      stepTarget[ax]   = stepPos[ax];
      isrRemaining[ax] = 0;
      interrupts();
      axHalted[ax]    = false;
      axWatchdog[ax]  = false;
      jogging[ax]     = false;
      jogRate[ax]     = 0;
      decelToStop[ax] = false;
      rate[ax]        = 0.0f;
      lastSetpointMs[ax] = millis();
      break;
    }

    case 'D': {
      // Commissioning diagnostic: raw levels, so a dead axis splits
      // into "the pin never moves" (wiring) and "the pin moves but
      // the count does not" (this firmware). Emitted as a comment,
      // which the host already discards.
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      Serial.print(F("# D ax="));  Serial.print(ax);
      Serial.print(F(" portb="));  Serial.print(PORTB, BIN);
      Serial.print(F(" dir="));    Serial.print(digitalRead(PIN_DIR[ax]));
      Serial.print(F(" alm="));    Serial.print(digitalRead(PIN_ALM[ax]));
      Serial.print(F(" home="));   Serial.print(digitalRead(PIN_HOME[ax]));
      Serial.print(F(" pos="));    Serial.print(axisPosition(ax));
      Serial.print(F(" inc="));    Serial.println(phaseInc[ax]);
      break;
    }

    case 'W': {
      // Real STEP pulses are one ISR period wide and only appear
      // while moving, so a meter cannot tell a working STEP line
      // from a dead one. This drives it at 1Hz, 50% duty.
      if (!axisValid(ax)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      noInterrupts(); TIMSK1 &= ~_BV(OCIE1A); interrupts();
      Serial.print(F("# W exercising STEP pin ")); Serial.print(PIN_STEP[ax]);
      Serial.println(F(" at 1Hz for 10s"));
      for (uint8_t i = 0; i < 10; i++) {
        digitalWrite(PIN_STEP[ax], HIGH); delay(500);
        digitalWrite(PIN_STEP[ax], LOW);  delay(500);
      }
      if (STEP_ACTIVE_LOW) PORTB |= STEP_MASK; else PORTB &= ~STEP_MASK;
      Serial.println(F("# W done"));
      break;
    }

    default:
      // A mismatched host is the likely cause, and it should be loud.
      replyFault(0, FAULT_BADCMD, "unknown command");
      break;
  }
}

void setup()
{
  Serial.begin(115200);

  for (uint8_t a = 0; a < AXIS_COUNT; a++) {
    pinMode(PIN_STEP[a], OUTPUT);
    pinMode(PIN_DIR[a],  OUTPUT);
    pinMode(PIN_ALM[a],  INPUT_PULLUP);
    if (STEP_HAS_ENABLE) pinMode(PIN_ENABLE[a], OUTPUT);
    pinMode(PIN_HOME[a], INPUT_PULLUP);
    lastSetpointMs[a] = millis();
    alarmStableMs[a]  = millis();
    setEnable(a, false);        // a no-op unless ENA is wired
    setDirection(a, true);
  }

  // Park STEP inactive before anything can pulse it.
  if (STEP_ACTIVE_LOW) PORTB |=  STEP_MASK;
  else                 PORTB &= ~STEP_MASK;

  // Timer1: CTC, prescaler 8, OC1A DISCONNECTED so the step pins
  // stay under ISR control. OCIE1A stays clear — an idle board
  // must cost zero interrupts.
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11);
  TCNT1  = 0;
  OCR1A  = (uint16_t)((TIMER1_HZ / STEP_ISR_HZ) - 1);
  TIMSK1 = 0;
  interrupts();

  followerLastUs = micros();

  Serial.println(F("# CamBot axis board v3 (FW4, 3x STEP) ready"));
}

void loop()
{
  serviceWatchdogs();
  serviceAlarms();
  serviceAxes();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      cmdBuf[cmdLen] = '\0';
      handleCommand(cmdBuf);
      cmdLen = 0;
    } else if (c != '\r') {
      if (cmdLen < CMD_BUF_LEN - 1) cmdBuf[cmdLen++] = c;
      else                          cmdLen = 0;   // overlong: drop, resync at newline
    }
  }
}
