/***************************************************************
 * CamBotTimeline — Axis Board Firmware v2
 *
 * Multi-axis motion board for the CamBot rig. Speaks the v2
 * axis-addressed protocol (see src/core/axis_protocol.h on the
 * host side — the two must agree).
 *
 * THIS REVISION (FW 3): adds axis 1, a CL57C closed-loop stepper
 * driven by STEP/DIR. The protocol is unchanged — it was already
 * axis-addressed — so this revision needs no host protocol work.
 *
 * Why v2 exists: v1's commands carried no axis address and its
 * replies were bare values, matched positionally against
 * whatever the host asked last. With two axes sharing one link
 * that mis-attribution is not an edge case, it is the norm.
 * Here every command names an axis and every reply leads with a
 * type character.
 *
 * ─── Wiring ──────────────────────────────────────────────────
 *   Axis 0 — DC servo (H-bridge channel A)
 *     PWM = pin 5, DIR = pin 4
 *     Encoder A = pin 2 (INT0), B = pin 3 (INT1)
 *     Home limit switch = pin 8 (to GND, internal pullup)
 *
 *   Axis 1 — CL57C closed-loop stepper
 *     STEP   = pin 9  (PB1, Timer1 CTC — see below)
 *     DIR    = pin 7  (PD7)
 *     ENABLE = pin 6  (PD6)
 *     ALM in = A0     (input_pullup, polled)
 *     Home   = A1     (optional; absent by default, see NO_HOME)
 *
 *   *** Arduino GND MUST be bonded to the CL57C signal ground. ***
 *   Without it the opto inputs float and the drive either misses
 *   steps or ignores STEP entirely, intermittently.
 *
 * ─── Why STEP is on pin 9 and nothing else ───────────────────
 * Pin 9 is PB1. The step ISR touches PORTB and only PORTB; the
 * main loop writes PORTD (DC DIR on PD4, stepper DIR on PD7,
 * ENABLE on PD6). Different ports means the ISR can never
 * interrupt a loop-side read-modify-write of the register it is
 * about to write — a race that would glitch a direction line
 * about once an hour and cost a day to find.
 *
 * Timer1 runs in CTC mode with OC1A DISCONNECTED, so pin 9 stays
 * a plain GPIO the ISR drives by hand. Hardware PWM on that pin
 * would give no step count, and the count is the whole point.
 *
 * Serial: 115200 baud, commands terminated by '\n'.
 *   Two axes at 50Hz do not fit in 9600 — one character costs
 *   1.04ms there, and a tick's traffic would exceed the 20ms
 *   budget. The host must match this rate.
 *
 * Host -> board:
 *   V                 version / capability
 *   A                 enumerate axes
 *   G <ax> <pwm>      DC drive, signed -255..255. Kicks watchdog.
 *   T <ax> <steps>    stepper absolute target. Kicks watchdog.
 *   J <ax> <rate>     stepper jog, signed steps/sec. Kicks it too.
 *   L <ax> <vmax> <amax>  stepper clamps, steps/s and steps/s^2
 *   E <ax> <0|1>      stepper ENABLE (torque, NOT an e-stop)
 *   Q <ax>            query position
 *   Z <ax>            zero position counter
 *   H <ax>            query home switch
 *   S <ax>            query status word
 *   R <ax>            clear latched Halted
 *   P                 ping
 *   X                 global stop
 *
 * Board -> host:
 *   V FW=3 PROTO=2 BOARD=UNO AXES=2 CAPS=DC,STEP
 *   A <ax> <kind>
 *   Q <ax> <long>
 *   H <ax> <0|1>
 *   S <ax> <flagsHex> <pos> <rate>
 *   P OK
 *   ! <ax> <code> <text>     asynchronous fault, any time
 *   # <text>                 comment, host ignores
 ***************************************************************/

#include <util/delay.h>

#define FW_VERSION     3
#define PROTO_VERSION  2
#define BOARD_NAME     "UNO"

// ─── Pins ─────────────────────────────────────────────────────
const uint8_t PIN_DC_PWM      = 5;
const uint8_t PIN_DC_DIR      = 4;
const uint8_t PIN_ENCODER_A   = 2;
const uint8_t PIN_ENCODER_B   = 3;
const uint8_t PIN_DC_HOME     = 8;

const uint8_t PIN_STEP        = 9;    // PB1
const uint8_t PIN_STEP_DIR    = 7;    // PD7
const uint8_t PIN_STEP_ENABLE = 6;    // PD6
const uint8_t PIN_STEP_ALM    = A0;
const uint8_t PIN_STEP_HOME   = A1;

// Direct port access for the STEP line — digitalWrite in an ISR
// costs more than the pulse itself.
#define STEP_PORT   PORTB
#define STEP_BIT    _BV(PB1)

const int MAX_PWM = 255;

// ─── Signal polarity ──────────────────────────────────────────
// CL57C inputs are opto-isolated and usually wired common-anode,
// so pulling the pin LOW turns the opto ON. Every one of these is
// verified by a specific step of the bench checklist — if the
// axis moves the wrong way, flip DIR_INVERT here rather than
// negating stepsPerUnit on the host, which would silently break
// homing and the travel limits.
const bool STEP_ACTIVE_LOW   = true;
const bool DIR_INVERT        = false;
const bool ENABLE_ACTIVE_LOW = true;
const bool ALARM_ACTIVE_LOW  = true;

// No home switch is fitted on the stepper axis in this rig — the
// host uses "Set Zero Here" instead. The H command still answers
// so the protocol stays uniform; it reports "not pressed".
const bool STEP_HAS_HOME = false;

// ─── Axes ─────────────────────────────────────────────────────
const uint8_t AXIS_DC    = 0;
const uint8_t AXIS_STEP  = 1;
const uint8_t AXIS_COUNT = 2;

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

// If the host stops sending setpoints mid-motion (USB drop, app
// crash), stop driving rather than running away.
const unsigned long MOTOR_WATCHDOG_MS = 500;

// A stepper decelerates out of a watchdog trip rather than
// stopping dead — an abrupt stop at speed is exactly how a
// closed-loop drive loses sync and how the rig gets jerked. If it
// is somehow still moving after this long, stop it anyway.
const unsigned long STEP_WATCHDOG_HARDSTOP_MS = 250;

// ─── Encoder (axis 0) ─────────────────────────────────────────
volatile long    encoderCount = 0;
volatile uint8_t lastEncoded  = 0;

// Quadrature transition table, indexed by (prev << 2) | current.
// Replaces the four-way comparison chain the v1 ISR ran per edge.
// Kept in RAM, not PROGMEM: 16 bytes, and pgm_read_byte would add
// back most of what the table saves.
const int8_t QUAD_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// ─── DC axis state ────────────────────────────────────────────
int           dcPwm             = 0;
unsigned long dcLastSetpointMs  = 0;
bool          dcHalted          = false;
bool          dcWatchdogTripped = false;

// ─── Stepper axis state ───────────────────────────────────────
// Written by the ISR, read by loop(): every multi-byte access
// from loop() must be wrapped in noInterrupts().
volatile long    stepPos       = 0;      // steps, signed, absolute
volatile long    isrRemaining  = 0;      // steps left in position mode
volatile bool    isrContinuous = false;  // jog: never stop on count
volatile int8_t  isrDelta      = 1;      // +1 / -1, applied per pulse

long stepTarget   = 0;        // absolute, what the host asked for
long stepJogRate  = 0;        // signed steps/s; 0 = position mode
bool stepJogging  = false;

// Ramp to a standstill wherever that lands, overriding both jog
// and position mode. Set by X and by the watchdog. It cannot be a
// position target: the axis travels during the ramp, so the
// target would fall behind it and the follower would reverse to
// go back for it.
bool stepDecelToStop = false;

float stepRate    = 0.0f;     // current signed rate, steps/s
long  stepVmax    = 8000;     // steps/s   — host overrides via L
long  stepAmax    = 40000;    // steps/s^2 — host overrides via L

bool stepEnabled  = false;
bool stepHalted   = false;
bool stepWatchdogTripped = false;
bool stepAlarm    = false;

unsigned long stepLastSetpointMs = 0;
unsigned long stepWatchdogAtMs   = 0;   // when the trip happened
unsigned long stepFollowerLastUs = 0;
unsigned long alarmStableSinceMs = 0;
bool          alarmRawLast       = false;

// Timer1 at prescaler 8 gives a 2MHz tick. OCR1A is 16-bit, so
// the slowest expressible rate is 2e6/65536 = 30.5 steps/s; below
// that we simply stop. The fastest useful rate on an Uno is far
// lower than the counter's limit — see the CPU budget in the
// README — so the ceiling that matters is STEP_RATE_CEILING.
const float STEP_RATE_MIN     = 32.0f;
const long  STEP_RATE_CEILING = 20000L;   // hard board limit
const unsigned long TIMER1_HZ = 2000000UL;

// The follower recomputes rate on this period. Fast enough that
// the ramp is smooth (at amax=40000 that is 80 steps/s per step),
// slow enough that its float sqrt costs a few percent of CPU.
const unsigned long FOLLOWER_PERIOD_US = 2000;

// ─── Command buffer ───────────────────────────────────────────
// A fixed buffer, not String: this board has 2KB of SRAM, v2
// commands are longer than v1's and arrive more often, and
// String concatenation at 115200 fragments the heap into an
// eventual hang mid-take.
const uint8_t CMD_BUF_LEN = 40;
char    cmdBuf[CMD_BUF_LEN];
uint8_t cmdLen = 0;

// ─── Forward declarations ─────────────────────────────────────
// The service functions below fault and stop before those helpers
// are defined. The .ino preprocessor would usually generate these
// for us; declaring them by hand means the sketch also compiles
// as plain C++ if it is ever built outside the Arduino IDE.
void replyFault(uint8_t axis, uint8_t code, const char* text);
void setDcPwm(int pwm);
void stepHardStop();
bool stepIsMoving();

// ─── Encoder ISR ──────────────────────────────────────────────
// Raw INT0/INT1 vectors reading PIND once, instead of
// attachInterrupt + two digitalReads. ~12us -> ~3us per edge.
//
// This is not a micro-optimisation, it is what makes the stepper
// possible: at 2709 counts/rev and 300 RPM the encoder fires
// 13.5k times a second. At 12us that is 16% of the CPU consumed
// before the step ISR gets a look in, and interrupt latency —
// not CPU percentage — is what makes a step ISR jitter.
//
// PIND is read, never written, so this cannot race the DIR and
// ENABLE writes loop() makes to PORTD.
static inline void encoderUpdate() __attribute__((always_inline));
static inline void encoderUpdate()
{
  const uint8_t p = PIND;
  // A is PD2 -> bit 1 of `encoded`; B is PD3 -> bit 0.
  const uint8_t encoded = ((p >> 1) & 0x02) | ((p >> 3) & 0x01);
  encoderCount += QUAD_TABLE[((lastEncoded << 2) | encoded) & 0x0F];
  lastEncoded = encoded;
}

ISR(INT0_vect) { encoderUpdate(); }
ISR(INT1_vect) { encoderUpdate(); }

// ─── Step ISR ─────────────────────────────────────────────────
// One pulse per compare match. Deliberately contains no
// digitalWrite, no float, no millis() and no division: at 8kHz it
// runs 8000 times a second and everything in here is multiplied
// by that.
//
// It counts steps down itself rather than letting the follower
// stop it, because the follower only runs every 2ms — at 8kHz
// that would be up to 16 steps of overshoot per move.
ISR(TIMER1_COMPA_vect)
{
  if (!isrContinuous && isrRemaining == 0) {
    TIMSK1 &= ~_BV(OCIE1A);      // idle costs nothing at all
    return;
  }

  if (STEP_ACTIVE_LOW) STEP_PORT &= ~STEP_BIT;
  else                 STEP_PORT |=  STEP_BIT;

  // CL57C wants >= 2.5us of pulse. Compile-time delay, no timer.
  _delay_us(3);

  if (STEP_ACTIVE_LOW) STEP_PORT |=  STEP_BIT;
  else                 STEP_PORT &= ~STEP_BIT;

  stepPos += isrDelta;
  if (!isrContinuous && isrRemaining > 0) isrRemaining--;
}

// ─── Stepper primitives ───────────────────────────────────────
void stepSetDirection(bool positive)
{
  const bool level = DIR_INVERT ? !positive : positive;
  digitalWrite(PIN_STEP_DIR, level ? HIGH : LOW);
  isrDelta = positive ? 1 : -1;
  // CL57C needs the DIR line settled before the next STEP edge.
  delayMicroseconds(10);
}

void stepSetEnable(bool on)
{
  stepEnabled = on;
  digitalWrite(PIN_STEP_ENABLE, (ENABLE_ACTIVE_LOW ? !on : on) ? HIGH : LOW);
}

/// Programs Timer1 for `rate` (unsigned steps/s) and arms or
/// disarms the compare interrupt. rate 0 stops stepping.
void stepApplyRate(float rate)
{
  if (rate < STEP_RATE_MIN) {
    TIMSK1 &= ~_BV(OCIE1A);
    return;
  }

  unsigned long ocr = (TIMER1_HZ / (unsigned long)rate);
  if (ocr < 1)     ocr = 1;
  if (ocr > 65535) ocr = 65535;

  noInterrupts();
  OCR1A = (uint16_t)(ocr - 1);
  // Re-arming while TCNT1 is already past the new OCR1A would
  // miss the compare and stall until the counter wrapped, so
  // reset the counter with it.
  if (TCNT1 > OCR1A) TCNT1 = 0;
  TIMSK1 |= _BV(OCIE1A);
  interrupts();
}

void stepHardStop()
{
  noInterrupts();
  TIMSK1 &= ~_BV(OCIE1A);
  isrRemaining  = 0;
  isrContinuous = false;
  stepTarget    = stepPos;
  interrupts();

  stepRate        = 0.0f;
  stepJogging     = false;
  stepJogRate     = 0;
  stepDecelToStop = false;
}

bool stepIsMoving()
{
  return (TIMSK1 & _BV(OCIE1A)) != 0;
}

// ─── Follower ─────────────────────────────────────────────────
// Turns "where the host wants axis 1" into a step rate, honouring
// vmax and amax. Runs from loop() every FOLLOWER_PERIOD_US.
//
// The trajectory itself is owned by the host — this only clamps.
// Firmware-side trajectory generation would mean the axis arrives
// when the board decides rather than when the keyframe says, and
// multi-axis sync is exactly that decision.
void serviceStepper()
{
  const unsigned long nowUs = micros();
  if ((unsigned long)(nowUs - stepFollowerLastUs) < FOLLOWER_PERIOD_US) return;
  const float dt = (float)(unsigned long)(nowUs - stepFollowerLastUs) / 1000000.0f;
  stepFollowerLastUs = nowUs;

  if (stepHalted || stepAlarm || !stepEnabled) {
    if (stepIsMoving()) stepHardStop();
    return;
  }

  noInterrupts();
  const long pos = stepPos;
  interrupts();

  // ─ Desired signed rate ─
  float desired;
  if (stepDecelToStop) {
    desired = 0.0f;
  } else if (stepJogging) {
    desired = (float)stepJogRate;
  } else {
    const long remaining = stepTarget - pos;
    if (remaining == 0) {
      // Already there. Snap the rate to zero rather than ramping
      // it down: the vStop clamp below is what brought us in, so
      // there is no real speed left to shed, and a lingering
      // non-zero rate would keep re-arming the timer for a
      // one-shot ISR and keep reporting ST_MOVING while parked.
      stepRate = 0.0f;
      noInterrupts();
      TIMSK1 &= ~_BV(OCIE1A);
      isrRemaining  = 0;
      isrContinuous = false;
      interrupts();
      return;
    } else {
      // Fastest speed from which amax can still stop in the
      // distance left. Without this the axis overshoots every
      // target by whatever its decel distance happens to be.
      const float dist  = (float)labs(remaining);
      const float vStop = sqrt(2.0f * (float)stepAmax * dist);
      float v = (float)stepVmax;
      if (vStop < v) v = vStop;
      desired = (remaining > 0) ? v : -v;
    }
  }

  const float ceiling = (float)min(stepVmax, STEP_RATE_CEILING);
  if (desired >  ceiling) desired =  ceiling;
  if (desired < -ceiling) desired = -ceiling;

  // ─ Slew toward it at amax ─
  const float maxDelta = (float)stepAmax * dt;
  float next = stepRate;
  if (desired > next)      next = min(desired, next + maxDelta);
  else if (desired < next) next = max(desired, next - maxDelta);

  // A reversal must pass through zero. Changing DIR while pulses
  // are in flight is how a drive loses count, and the physics
  // says the same thing.
  if ((next > 0.0f && stepRate < 0.0f) || (next < 0.0f && stepRate > 0.0f)) {
    next = 0.0f;
  }
  stepRate = next;

  const float mag = fabs(stepRate);
  if (mag <= 0.0f) {
    noInterrupts();
    TIMSK1 &= ~_BV(OCIE1A);
    isrRemaining  = 0;
    isrContinuous = false;
    if (stepDecelToStop) stepTarget = stepPos;   // adopt where we stopped
    interrupts();
    stepDecelToStop = false;
    return;
  }

  // Timer1 cannot express anything slower than STEP_RATE_MIN (a
  // 16-bit compare register at 2MHz), so a slower ramp value gets
  // CLOCKED at the floor — it must not be clamped back to zero.
  //
  // Zeroing it deadlocks the axis: the ramp's first increment is
  // amax*dt, which for FOLLOWER_PERIOD_US=2000 is amax/500, so any
  // amax below ~16000 steps/s^2 produces a first step under the
  // floor. The rate would climb to that value, get zeroed, climb
  // again, and the axis would sit still forever while reporting
  // itself healthy and not moving.
  //
  // stepRate itself keeps the true ramp value, so acceleration
  // continues normally underneath; only what Timer1 is programmed
  // with is raised. The cost is that motion begins at 32 steps/s
  // rather than creeping in from zero, which at 1600 p/r is 0.02
  // rev/s — below anything the rig can resolve.
  const float clocked = (mag < STEP_RATE_MIN) ? STEP_RATE_MIN : mag;

  const bool positive = stepRate > 0.0f;
  if ((positive && isrDelta < 0) || (!positive && isrDelta > 0)) {
    stepSetDirection(positive);
  }

  // Hand the ISR the remaining count so it stops exactly on
  // target rather than waiting for the next follower pass.
  noInterrupts();
  if (stepJogging) {
    isrContinuous = true;
    isrRemaining  = 0;
  } else {
    isrContinuous = false;
    const long remaining = stepTarget - stepPos;
    isrRemaining = (remaining >= 0) ? remaining : -remaining;
  }
  interrupts();

  stepApplyRate(clocked);
}

// ─── Alarm ────────────────────────────────────────────────────
// The CL57C closes its own loop, so the host's idea of the
// stepper's position is what it ASKED for, not what happened. A
// missed step is invisible. ALM is the only integrity signal this
// axis has, which is why it is polled every pass and why tripping
// it latches Halted rather than just reporting.
void serviceAlarm()
{
  const bool raw = (digitalRead(PIN_STEP_ALM) == LOW) == ALARM_ACTIVE_LOW;
  const unsigned long now = millis();

  if (raw != alarmRawLast) {
    alarmRawLast       = raw;
    alarmStableSinceMs = now;
    return;
  }

  // 20ms of agreement before believing it — an unshielded run
  // next to a stepper's phase wires picks up plenty of noise.
  if ((now - alarmStableSinceMs) < 20) return;

  if (raw && !stepAlarm) {
    stepAlarm  = true;
    stepHalted = true;
    // No graceful decel here, unlike the watchdog: the drive has
    // already faulted and the count is already wrong. Stop.
    stepHardStop();
    replyFault(AXIS_STEP, FAULT_ALARM, "drive alarm");
  } else if (!raw && stepAlarm) {
    // Clears the alarm condition, NOT the latched halt — that
    // still needs an explicit R from the host.
    stepAlarm = false;
  }
}

// ─── Watchdogs ────────────────────────────────────────────────
void serviceWatchdogs()
{
  const unsigned long now = millis();

  // Axis 0: only 'G' refreshes it, so a host that stops streaming
  // setpoints stops the motor rather than leaving it driving at
  // the last commanded PWM.
  if (dcPwm != 0 && (now - dcLastSetpointMs) > MOTOR_WATCHDOG_MS) {
    setDcPwm(0);
    if (!dcWatchdogTripped) {
      dcWatchdogTripped = true;
      replyFault(AXIS_DC, FAULT_WATCHDOG, "setpoint stream stopped");
    }
  }

  // Axis 1: watches the SETPOINT stream (T/J) specifically, not
  // "any command". A host that keeps polling Q while its playback
  // thread has died must not keep a stale target alive.
  if (!stepWatchdogTripped) {
    if (stepIsMoving() && (now - stepLastSetpointMs) > MOTOR_WATCHDOG_MS) {
      stepWatchdogTripped = true;
      stepWatchdogAtMs    = now;
      // Ramp down at amax rather than stopping dead — an abrupt
      // stop at speed is how a closed-loop drive loses sync and
      // how the rig gets jerked. Halted latches once the ramp
      // finishes, so no late T can revive the move mid-decel.
      stepDecelToStop = true;
      stepJogging     = false;
      stepJogRate     = 0;
      replyFault(AXIS_STEP, FAULT_WATCHDOG, "setpoint stream stopped");
    }
  } else if (stepIsMoving() && (now - stepWatchdogAtMs) > STEP_WATCHDOG_HARDSTOP_MS) {
    // Decel should have finished by now. It hasn't, so stop dead
    // and accept the lost sync — still better than running on.
    stepHardStop();
    stepHalted = true;
  } else if (!stepIsMoving() && !stepHalted) {
    stepHalted = true;   // decel complete; recovery needs an R
  }
}

// ASSUMPTION: verify against hardware — positive PWM should drive
// toward increasing position; swap the DIR polarity if reversed.
void setDcPwm(int pwm)
{
  pwm = constrain(pwm, -MAX_PWM, MAX_PWM);
  dcPwm = pwm;
  digitalWrite(PIN_DC_DIR, pwm >= 0 ? HIGH : LOW);
  analogWrite(PIN_DC_PWM, abs(pwm));
}

// ─── Reply helpers ────────────────────────────────────────────
void replyFault(uint8_t axis, uint8_t code, const char* text)
{
  Serial.print('!');  Serial.print(' ');
  Serial.print(axis); Serial.print(' ');
  Serial.print(code); Serial.print(' ');
  Serial.println(text);
}

bool axisValid(long axis)
{
  return axis >= 0 && axis < AXIS_COUNT;
}

long axisPosition(uint8_t axis)
{
  long v;
  noInterrupts();
  v = (axis == AXIS_DC) ? encoderCount : stepPos;
  interrupts();
  return v;
}

uint8_t dcStatusFlags()
{
  uint8_t f = ST_ENABLED;                        // an H-bridge is always "enabled"
  if (dcPwm != 0)                      f |= ST_MOVING;
  if (digitalRead(PIN_DC_HOME) == LOW) f |= ST_HOME_SWITCH;
  if (dcHalted)                        f |= ST_HALTED;
  if (dcWatchdogTripped)               f |= ST_WATCHDOG;
  return f;
}

uint8_t stepStatusFlags()
{
  uint8_t f = 0;
  if (stepEnabled)         f |= ST_ENABLED;
  if (stepIsMoving())      f |= ST_MOVING;
  if (stepAlarm)           f |= ST_ALARM;
  if (stepHalted)          f |= ST_HALTED;
  if (stepWatchdogTripped) f |= ST_WATCHDOG;
  // An alarm means the drive lost its own loop, so the step count
  // the host has been trusting no longer describes the shaft.
  if (stepAlarm)           f |= ST_POSITION_LOST;
  if (STEP_HAS_HOME && digitalRead(PIN_STEP_HOME) == LOW) f |= ST_HOME_SWITCH;
  return f;
}

// ─── Command dispatch ─────────────────────────────────────────
void handleCommand(char* line)
{
  if (line[0] == '\0') return;

  const char type = line[0];

  // Tokenise past the type character. strtok is fine here: the
  // parser is single-threaded and never re-enters.
  strtok(line, " ");
  char* a1 = strtok(NULL, " ");
  char* a2 = strtok(NULL, " ");
  char* a3 = strtok(NULL, " ");

  const long axis = a1 ? atol(a1) : -1;

  switch (type) {

    case 'V':
      Serial.print(F("V FW="));    Serial.print(FW_VERSION);
      Serial.print(F(" PROTO="));  Serial.print(PROTO_VERSION);
      Serial.print(F(" BOARD="));  Serial.print(F(BOARD_NAME));
      Serial.print(F(" AXES="));   Serial.print(AXIS_COUNT);
      Serial.println(F(" CAPS=DC,STEP"));
      break;

    case 'A':
      Serial.print(F("A ")); Serial.print(AXIS_DC);   Serial.println(F(" DC"));
      Serial.print(F("A ")); Serial.print(AXIS_STEP); Serial.println(F(" STEP"));
      break;

    case 'P':
      Serial.println(F("P OK"));
      break;

    case 'X':
      // Global stop. Deliberately does NOT latch Halted — this is
      // the operator's stop, not a fault, and must not require an
      // explicit R to recover from.
      //
      // Nor does it drop ENABLE. Cutting torque on a gravity-
      // loaded axis makes it fall; stopping means decelerate and
      // hold, and holding needs the drive energised.
      setDcPwm(0);
      stepDecelToStop = true;
      stepJogging     = false;
      stepJogRate     = 0;
      break;

    case 'G': {
      if (axis != AXIS_DC) { replyFault(0, FAULT_BADCMD, "G: not a DC axis"); break; }
      if (!a2)             { replyFault(axis, FAULT_BADCMD, "missing pwm"); break; }
      if (dcHalted)        break;   // latched fault: ignore until R
      setDcPwm(atoi(a2));
      dcLastSetpointMs = millis();
      break;
    }

    case 'T': {
      if (axis != AXIS_STEP) { replyFault(0, FAULT_BADCMD, "T: not a step axis"); break; }
      if (!a2)               { replyFault(axis, FAULT_BADCMD, "missing target"); break; }
      if (stepHalted || stepAlarm) break;   // latched: ignore until R
      stepTarget      = atol(a2);
      stepJogging     = false;
      stepJogRate     = 0;
      stepDecelToStop = false;
      stepLastSetpointMs = millis();
      break;
    }

    case 'J': {
      if (axis != AXIS_STEP) { replyFault(0, FAULT_BADCMD, "J: not a step axis"); break; }
      if (!a2)               { replyFault(axis, FAULT_BADCMD, "missing rate"); break; }
      if (stepHalted || stepAlarm) break;
      const long rate = atol(a2);
      if (labs(rate) > STEP_RATE_CEILING) {
        replyFault(axis, FAULT_OVERRATE, "above board step ceiling");
        break;
      }
      stepJogRate     = rate;
      stepJogging     = (rate != 0);
      stepDecelToStop = false;
      if (!stepJogging) {
        // Leaving jog: adopt where we are as the target so the
        // follower decelerates in place instead of snapping back
        // to whatever T was last sent.
        noInterrupts();
        stepTarget = stepPos;
        interrupts();
      }
      stepLastSetpointMs = millis();
      break;
    }

    case 'L': {
      if (axis != AXIS_STEP) { replyFault(0, FAULT_BADCMD, "L: not a step axis"); break; }
      if (!a2 || !a3)        { replyFault(axis, FAULT_BADCMD, "missing limits"); break; }
      const long v = atol(a2);
      const long a = atol(a3);
      if (v > 0) stepVmax = min(v, STEP_RATE_CEILING);
      if (a > 0) stepAmax = a;
      break;
    }

    case 'E': {
      if (axis != AXIS_STEP) { replyFault(0, FAULT_BADCMD, "E: not a step axis"); break; }
      if (!a2)               { replyFault(axis, FAULT_BADCMD, "missing state"); break; }
      const bool on = (atoi(a2) != 0);
      if (!on) stepHardStop();     // never drop torque mid-pulse
      stepSetEnable(on);
      break;
    }

    case 'Q': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      Serial.print(F("Q ")); Serial.print(axis);
      Serial.print(' ');     Serial.println(axisPosition(axis));
      break;
    }

    case 'Z': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (axis == AXIS_DC) {
        noInterrupts();
        encoderCount = 0;
        interrupts();
      } else {
        // Target moves with the origin, or zeroing would command
        // an immediate run back to the old target's step count.
        noInterrupts();
        stepPos      = 0;
        isrRemaining = 0;
        interrupts();
        stepTarget = 0;
      }
      break;
    }

    case 'H': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      bool pressed;
      if (axis == AXIS_DC) pressed = (digitalRead(PIN_DC_HOME) == LOW);
      else                 pressed = STEP_HAS_HOME && (digitalRead(PIN_STEP_HOME) == LOW);
      Serial.print(F("H ")); Serial.print(axis);
      Serial.print(' ');     Serial.println(pressed ? 1 : 0);
      break;
    }

    case 'S': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      Serial.print(F("S "));  Serial.print(axis);
      Serial.print(' ');
      Serial.print(axis == AXIS_DC ? dcStatusFlags() : stepStatusFlags(), HEX);
      Serial.print(' ');      Serial.print(axisPosition(axis));
      Serial.print(' ');      Serial.println(axis == AXIS_DC ? (long)dcPwm : (long)stepRate);
      break;
    }

    case 'R': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (axis == AXIS_DC) {
        dcHalted = false;
        dcWatchdogTripped = false;
        dcLastSetpointMs = millis();
      } else {
        if (stepAlarm) {
          // Still asserted. Clearing the halt would just let the
          // host command a move into a faulted drive.
          replyFault(axis, FAULT_ALARM, "alarm still asserted");
          break;
        }
        // Adopt the current position as the target, so a resumed
        // link can never continue a move the operator has since
        // lost track of.
        noInterrupts();
        stepTarget   = stepPos;
        isrRemaining = 0;
        interrupts();
        stepHalted          = false;
        stepWatchdogTripped = false;
        stepJogging         = false;
        stepJogRate         = 0;
        stepDecelToStop     = false;
        stepRate            = 0.0f;
        stepLastSetpointMs  = millis();
      }
      break;
    }

    default:
      // Unknown type. Say so rather than failing silently — a
      // mismatched host is the likely cause and should be loud.
      replyFault(0, FAULT_BADCMD, "unknown command");
      break;
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_DC_PWM,    OUTPUT);
  pinMode(PIN_DC_DIR,    OUTPUT);
  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  pinMode(PIN_DC_HOME,   INPUT_PULLUP);

  pinMode(PIN_STEP,        OUTPUT);
  pinMode(PIN_STEP_DIR,    OUTPUT);
  pinMode(PIN_STEP_ENABLE, OUTPUT);
  pinMode(PIN_STEP_ALM,    INPUT_PULLUP);
  pinMode(PIN_STEP_HOME,   INPUT_PULLUP);

  // Park STEP in its inactive level before anything can pulse it.
  if (STEP_ACTIVE_LOW) STEP_PORT |=  STEP_BIT;
  else                 STEP_PORT &= ~STEP_BIT;

  // Encoder: raw INT0/INT1, both on any logical change.
  lastEncoded = ((PIND >> 1) & 0x02) | ((PIND >> 3) & 0x01);
  EICRA = (1 << ISC00) | (1 << ISC10);
  EIMSK = (1 << INT0)  | (1 << INT1);

  // Timer1: CTC (mode 4), prescaler 8, OC1A DISCONNECTED so pin 9
  // stays under ISR control. OCIE1A stays clear — an idle stepper
  // must cost zero interrupts, or the encoder pays for it.
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11);
  TCNT1  = 0;
  OCR1A  = 65535;
  TIMSK1 = 0;
  interrupts();

  stepSetDirection(true);
  stepSetEnable(false);        // no torque until the host asks

  setDcPwm(0);
  dcLastSetpointMs   = millis();
  stepLastSetpointMs = millis();
  stepFollowerLastUs = micros();
  alarmStableSinceMs = millis();

  // Announce on boot so a serial monitor shows the board is alive
  // and which firmware is on it. The host ignores '#' lines.
  Serial.println(F("# CamBot axis board v2 (FW3, DC + STEP) ready"));
}

void loop()
{
  serviceWatchdogs();
  serviceAlarm();
  serviceStepper();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      cmdBuf[cmdLen] = '\0';
      handleCommand(cmdBuf);
      cmdLen = 0;
    } else if (c != '\r') {
      if (cmdLen < CMD_BUF_LEN - 1) {
        cmdBuf[cmdLen++] = c;
      } else {
        // Overlong line: drop it rather than overflowing. Resync
        // happens at the next newline.
        cmdLen = 0;
      }
    }
  }
}
