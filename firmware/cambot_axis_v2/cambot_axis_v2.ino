/***************************************************************
 * CamBotTimeline — Axis Board Firmware v2
 *
 * Multi-axis motion board for the CamBot rig. Speaks the v2
 * axis-addressed protocol (see src/core/axis_protocol.h on the
 * host side — the two must agree).
 *
 * THIS REVISION: axis 0 (DC servo) only. The CL57C stepper axis
 * arrives in the next revision; the protocol is already
 * axis-addressed so that change needs no host protocol work.
 *
 * Why v2 exists: v1's commands carried no axis address and its
 * replies were bare values, matched positionally against
 * whatever the host asked last. With two axes sharing one link
 * that mis-attribution is not an edge case, it is the norm.
 * Here every command names an axis and every reply leads with a
 * type character.
 *
 * Wiring (unchanged from v1):
 *   DC motor  PWM = pin 5, DIR = pin 4   (H-bridge channel A)
 *   Encoder A = pin 2 (INT0), B = pin 3 (INT1)
 *   Home limit switch = pin 8 (to GND, internal pullup)
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
 *   Q <ax>            query position
 *   Z <ax>            zero position counter
 *   H <ax>            query home switch
 *   S <ax>            query status word
 *   R <ax>            clear latched Halted
 *   P                 ping
 *   X                 global stop
 *
 * Board -> host:
 *   V FW=2 PROTO=2 BOARD=UNO AXES=1 CAPS=DC
 *   A <ax> <kind>
 *   Q <ax> <long>
 *   H <ax> <0|1>
 *   S <ax> <flagsHex> <pos> <rate>
 *   P OK
 *   ! <ax> <code> <text>     asynchronous fault, any time
 *   # <text>                 comment, host ignores
 ***************************************************************/

#define FW_VERSION     2
#define PROTO_VERSION  2
#define BOARD_NAME     "UNO"

// ─── Pins ─────────────────────────────────────────────────────
const uint8_t PIN_DC_PWM    = 5;
const uint8_t PIN_DC_DIR    = 4;
const uint8_t PIN_ENCODER_A = 2;
const uint8_t PIN_ENCODER_B = 3;
const uint8_t PIN_DC_HOME   = 8;

const int MAX_PWM = 255;

// Axis indices. Only the DC axis exists in this revision.
const uint8_t AXIS_DC    = 0;
const uint8_t AXIS_COUNT = 1;

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

// ─── Encoder ──────────────────────────────────────────────────
volatile long    encoderCount = 0;
volatile uint8_t lastEncoded  = 0;

// ─── DC axis state ────────────────────────────────────────────
int           dcPwm            = 0;
unsigned long dcLastSetpointMs = 0;
bool          dcHalted         = false;
bool          dcWatchdogTripped = false;

// ─── Command buffer ───────────────────────────────────────────
// A fixed buffer, not String: this board has 2KB of SRAM, v2
// commands are longer than v1's and arrive more often, and
// String concatenation at 115200 fragments the heap into an
// eventual hang mid-take.
const uint8_t CMD_BUF_LEN = 40;
char    cmdBuf[CMD_BUF_LEN];
uint8_t cmdLen = 0;

// ─── Encoder ISR ──────────────────────────────────────────────
// NOTE: still digitalRead-based, unchanged from v1 on purpose.
// This revision changes the protocol only, so that if the DC
// axis misbehaves there is exactly one suspect. The port-read
// optimisation (~12us -> ~3us, needed to make room for the
// stepper's Timer1 ISR) lands with the stepper revision.
void encoderISR()
{
  uint8_t a = digitalRead(PIN_ENCODER_A);
  uint8_t b = digitalRead(PIN_ENCODER_B);
  uint8_t encoded = (a << 1) | b;
  uint8_t sum = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderCount++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderCount--;

  lastEncoded = encoded;
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

uint8_t dcStatusFlags()
{
  uint8_t f = ST_ENABLED;                      // an H-bridge is always "enabled"
  if (dcPwm != 0)                    f |= ST_MOVING;
  if (digitalRead(PIN_DC_HOME) == LOW) f |= ST_HOME_SWITCH;
  if (dcHalted)                      f |= ST_HALTED;
  if (dcWatchdogTripped)             f |= ST_WATCHDOG;
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

  const long axis = a1 ? atol(a1) : -1;

  switch (type) {

    case 'V':
      Serial.print(F("V FW="));    Serial.print(FW_VERSION);
      Serial.print(F(" PROTO="));  Serial.print(PROTO_VERSION);
      Serial.print(F(" BOARD="));  Serial.print(F(BOARD_NAME));
      Serial.print(F(" AXES="));   Serial.print(AXIS_COUNT);
      Serial.println(F(" CAPS=DC"));
      break;

    case 'A':
      Serial.print(F("A "));
      Serial.print(AXIS_DC);
      Serial.println(F(" DC"));
      break;

    case 'P':
      Serial.println(F("P OK"));
      break;

    case 'X':
      // Global stop. Deliberately does NOT latch Halted — this is
      // the operator's stop, not a fault, and must not require an
      // explicit R to recover from.
      setDcPwm(0);
      break;

    case 'G': {
      if (!axisValid(axis))  { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      if (!a2)               { replyFault(axis, FAULT_BADCMD, "missing pwm"); break; }
      if (dcHalted)          break;   // latched fault: ignore until R
      setDcPwm(atoi(a2));
      dcLastSetpointMs = millis();
      break;
    }

    case 'Q': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      noInterrupts();
      long count = encoderCount;
      interrupts();
      Serial.print(F("Q ")); Serial.print(axis);
      Serial.print(' ');     Serial.println(count);
      break;
    }

    case 'Z': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      noInterrupts();
      encoderCount = 0;
      interrupts();
      break;
    }

    case 'H': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      bool pressed = (digitalRead(PIN_DC_HOME) == LOW);
      Serial.print(F("H ")); Serial.print(axis);
      Serial.print(' ');     Serial.println(pressed ? 1 : 0);
      break;
    }

    case 'S': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      noInterrupts();
      long count = encoderCount;
      interrupts();
      Serial.print(F("S "));  Serial.print(axis);
      Serial.print(' ');      Serial.print(dcStatusFlags(), HEX);
      Serial.print(' ');      Serial.print(count);
      Serial.print(' ');      Serial.println(dcPwm);
      break;
    }

    case 'R': {
      if (!axisValid(axis)) { replyFault(0, FAULT_BADCMD, "bad axis"); break; }
      dcHalted = false;
      dcWatchdogTripped = false;
      dcLastSetpointMs = millis();
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

  lastEncoded = (digitalRead(PIN_ENCODER_A) << 1) | digitalRead(PIN_ENCODER_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), encoderISR, CHANGE);

  setDcPwm(0);
  dcLastSetpointMs = millis();

  // Announce on boot so a serial monitor shows the board is alive
  // and which firmware is on it. The host ignores '#' lines.
  Serial.println(F("# CamBot axis board v2 ready"));
}

void loop()
{
  // Watchdog: only 'G' refreshes it, so a host that stops
  // streaming setpoints stops the motor rather than leaving it
  // driving at the last commanded PWM.
  if (dcPwm != 0 && (millis() - dcLastSetpointMs) > MOTOR_WATCHDOG_MS) {
    setDcPwm(0);
    if (!dcWatchdogTripped) {
      dcWatchdogTripped = true;
      replyFault(AXIS_DC, FAULT_WATCHDOG, "setpoint stream stopped");
    }
  }

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
