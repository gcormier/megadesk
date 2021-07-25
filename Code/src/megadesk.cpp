// want to override minimum/maximum heights?
#define MINMAX

// option to include immediate user-feedback pips
#define FEEDBACK

// experimental smaller linInit
#define SMALL_INIT

// enable reset via button-press and/or bad linInit
#define ENABLERESET

// Serial control/telemetry
#define SERIALCOMMS
// Transmit/receive ascii commands over serial for human interface/control.
#define HUMANSERIAL
// Echo back the interpreted command before acting on it.
#define SERIALECHO
// Report errors over serial
#define SERIALERRORS

// Report the variant (idle) type on move
//#define SERIAL_IDLE

// raw data. turn off HUMANSERIAL,SERIALECHO & use hexlify..
// Debug of packets sent during init over serial.
//#define DEBUGSTARTUP
// Debug of encoder values in linBurst.
//#define DEBUG_ENCODER

// easter egg
#define EASTER

#include <EEPROM.h>
#include "lin.h"
#include "megadesk.h"
#include <avr/wdt.h>

// constants related to presses/eeprom slots
// (on attiny841: 512byte eeprom means slots 0-255)
// EEPROM magic signature to detect if eeprom is valid
#define EEPROM_SIG_SLOT  0
#define MAGIC_SIG    0x120d // bytes: 13, 18 in little endian order
#define MIN_SLOT         2  // 1 is possible but cant save without serial
#define MIN_HEIGHT_SLOT  11
#define MAX_HEIGHT_SLOT  12
#define RECALIBRATE      14 // nothing is stored there
#ifdef ENABLERESET
#define FORCE_RESET      15  // force reset
#endif
#define RESERVED_VARIANT 16 // reserved - deliberately empty
#define FEEDBACK_SLOT    17 // short tones on every button-press. buzz on no-ops
#define BOTHBUTTON_SLOT  18 // store whether bothbuttons is enabled
#define DOWN_SLOT_START  32 // 0x20 offset for down button slots

#ifdef SERIALCOMMS
uint16_t oldHeight = 0; // previously reported height
#endif

#define HYSTERESIS 137
#define PIN_UP 10
#define PIN_DOWN 9
#define PIN_BEEP 7
#define PIN_SERIAL 1

// click durations
#define CLICK_TIMEOUT   400UL // Timeout in MS. for long-hold and release idle.
#define CLICK_LONG    10000UL // very-long holdtime in MS.

// beeps
#define PIP_DURATION 20
#define SHORT_PAUSE 50
#define BEEP_DURATION 150
#define PAUSE BEEP_DURATION
#define LONG_PAUSE 500
#define ONE_SEC_PAUSE 1000
#define PITCH_ADJUST (48000000 / F_CPU) // digitalWrite takes ~6us at 8MHz

// notes to beep
#define SILENCE 20
#define NOTE_A4 440
#define NOTE_C6 1046
#define NOTE_CSHARP6 1109
#define NOTE_D6 1175
#define NOTE_DSHARP6 1245
#define NOTE_E6 1319
#define NOTE_F6 1397
#define NOTE_G6 1568
#define NOTE_A6 1760
#define NOTE_B6 1976
#define NOTE_C7 2093
#define NOTE_CSHARP7 2217
#define NOTE_D7 2344
#define NOTE_DSHARP7 2489
#define NOTE_E7 2637
#define NOTE_F7 2794
#define NOTE_G7 3136
#define NOTE_A7 3520
#define NOTE_B7 3951
#define NOTE_C8 4186
#define NOTE_LOW NOTE_C6
#define NOTE_ACK NOTE_G6
#define NOTE_HIGH NOTE_C7

#define FINE_MOVEMENT_VALUE 100 // Based on protocol decoding

// LIN commands/status
#define LIN_CMD_IDLE 252
#define LIN_CMD_RAISE 134
#define LIN_CMD_LOWER 133
#define LIN_CMD_FINE 135
#define LIN_CMD_FINISH 132
#define LIN_CMD_PREMOVE 196
#define LIN_CMD_RECALIBRATE 189
#define LIN_CMD_RECALIBRATE_END 188
#define LIN_MOTOR_BUSY 2
#define LIN_MOTOR_BUSY_FINE 3

// These are the 3 known idle states
#define LIN_MOTOR_IDLE1 0
#define LIN_MOTOR_IDLE2 37
#define LIN_MOTOR_IDLE3 96

// Changing these might be a really bad idea. They are sourced from
// decoding the OEM controller limits.
#define DANGER_MAX_HEIGHT (6777 - HYSTERESIS)
#define DANGER_MIN_HEIGHT (162 + HYSTERESIS)

// any button pressed?
#define PRESSED(b) (b != Button::NONE)
// is a memory button?
#define MEMORY_BUTTON(b) ((b == Button::UP) || ( bothbuttons && (b == Button::DOWN)))
// is bothbuttons and the down button was used?
#define ADJUST_DOWN ((bothbuttons) && (lastbutton == Button::DOWN))

// Tracking multipress/longpress of buttons
Button previous = Button::NONE; // previous state of buttons
Button lastbutton = Button::NONE; // last button to be pressed
uint8_t pushCount = 0; // tracks number of button pushes
uint8_t bothbuttons; // both button mode
bool savePosition = false; // flag to save currentHeight to pushCount slot
bool memoryEvent = false; // flag to load/save a pushCount slot
Command manualMove; // press and hold up/down?

// feedback pips
#ifdef FEEDBACK
bool feedback;
uint16_t scale[] = { NOTE_C6, NOTE_D6, NOTE_E6, NOTE_F6, NOTE_G6, NOTE_A6, NOTE_B6, NOTE_C7, };
#endif

// timestamps
unsigned long lastPushTime = 0;
unsigned long refTime = 0;
//////////////////

Lin lin(Serial, PIN_SERIAL);

uint16_t currentHeight = 0;
uint16_t targetHeight = 0;

uint16_t maxHeight = DANGER_MAX_HEIGHT;
uint16_t minHeight = DANGER_MIN_HEIGHT;

bool memoryMoving = false;
Command user_cmd = Command::NONE; // for requesting motors up/down
State state = State::OFF; // desk-protocol state

// set/clear motor up command
#define MOTOR_UP   user_cmd = Command::UP
#define MOTOR_DOWN user_cmd = Command::DOWN
#define MOTOR_OFF  user_cmd = Command::NONE

#ifdef EASTER
#define EIGHTH 131
const uint16_t tones[] = {
  NOTE_C6, EIGHTH*2, NOTE_C7, EIGHTH*8,
  NOTE_B6, EIGHTH, NOTE_C7, EIGHTH,
  NOTE_B6, EIGHTH, NOTE_G6, EIGHTH,
  NOTE_A6, EIGHTH*8,
  NOTE_F6, EIGHTH, SILENCE, EIGHTH,
  NOTE_F6, EIGHTH*2,
  NOTE_F6, EIGHTH, NOTE_E6, EIGHTH,
  NOTE_D6, EIGHTH, NOTE_E6, EIGHTH,
  NOTE_C6, EIGHTH, SILENCE, EIGHTH, };
#endif

// clean the slate for button presses
void startFresh()
{
  previous = Button::NONE;
  lastPushTime = 0;
  pushCount = 0;
  savePosition = false;
  memoryEvent = false;
}

#ifdef ENABLERESET
// use a constructor to disable the watchdog well before setup() is called
softReset::softReset()
{
    MCUSR = 0;
    wdt_disable();
}
softReset soft;

// watchdog software-reset method.
inline void softReset::Reset() {
  wdt_enable( WDTO_30MS );
  // avoid wdt_reset() with this loop
  for(;;) {}
}
#endif


void setup()
{
  bool up_press = false;
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BEEP, OUTPUT);
  delay(SHORT_PAUSE);

  // hold up button on boot to toggle both/single Button Mode
  if (!digitalRead(PIN_UP))
    up_press = true;

  // hold down button during reset to factory reset
  if (!digitalRead(PIN_DOWN))
  {
    initAndReadEEPROM(true);
    beep(NOTE_C7, 2);
    delay(LONG_PAUSE);
    while (true)
    {
      if (!digitalRead(PIN_DOWN))
      {
        // descending tones
        beep(NOTE_E7);
        beep(NOTE_D7);
        beep(NOTE_C7);
        delay(LONG_PAUSE);
      }
      if (!digitalRead(PIN_UP))
      {
        // ascending tones
        beep(NOTE_C7);
        beep(NOTE_D7);
        beep(NOTE_E7);
        delay(LONG_PAUSE);
      }
      delay(SHORT_PAUSE);
    }

  }

#ifdef SERIALCOMMS
  Serial1.begin(115200);
#endif

  // init + arpeggio
  beep(NOTE_C6);

  initAndReadEEPROM(false);
  beep(NOTE_E6);

  lin.begin(19200);
  beep(NOTE_G6);
#ifdef DEBUGSTARTUP
  // output bit period and recv-timeout times.
  writeSerial(1000000/19200, (34+90)*1000000/19200, 0xaa);
#endif

  linInit();

  // final note played once for single button mode, twice for bothbutton mode
  if (up_press)
    toggleBothMode();
  else
    beep(NOTE_C7, bothbuttons+1);
}

// track button presses - short and long
void readButtons()
{
  Button buttons;
  unsigned long currentMillis = millis();

  if (!digitalRead(PIN_UP)) {
    if (!digitalRead(PIN_DOWN)) {
      buttons = Button::BOTH;
      manualMove = Command::NONE;
    } else {
      buttons = Button::UP;
    }
  } else if (!digitalRead(PIN_DOWN)) {
    buttons = Button::DOWN;
  } else {
    buttons = Button::NONE;
  }

  if ( PRESSED(buttons) )
  { // a button is pressed
    if (previous != buttons)
    { // new push

      // clear pushCount if pushing a different button from last
      if (buttons != lastbutton)
        startFresh();

      // If already moving and any button is pressed - stop!
      if ( memoryMoving )
        targetHeight = currentHeight;

      lastPushTime = currentMillis;
      lastbutton = buttons;

#ifdef FEEDBACK
      if (feedback)
        playTone(scale[pushCount % (sizeof(scale)/sizeof(scale[0]))],
                PIP_DURATION); // musical feedback
#endif
    }
    else
    { // button held
      unsigned long pushLength = currentMillis - lastPushTime;

      // long push?
      if (pushLength > CLICK_TIMEOUT) {
        // first long push? then move!
        if (pushCount == 0) {
          if (buttons == Button::UP) manualMove = Command::UP;
          if (buttons == Button::DOWN) manualMove = Command::DOWN;
#ifdef EASTER
          if ((buttons == Button::BOTH) && (pushLength > CLICK_LONG)) {
            // 10s hold. unused trigger, play the easter-egg
            for (uint16_t i=0; i < sizeof(tones)/sizeof(tones[0]); i+=2) {
              playTone(tones[i], tones[i+1]);
            }
          }
#endif
        }
        else if ( MEMORY_BUTTON(buttons) && (!savePosition) )
        {
          // longpress after previous pushes - save this position
          beep(NOTE_ACK, pushCount + 1); // first provide feedback to release...
          savePosition = true;
        }
      }
    }
  }
  else
  { // nothing currently pressed
    if ( PRESSED(previous) )
    { // just released
      // moving?
      if (manualMove != Command::NONE)
      {
        // we were under manual control before, stop now
        manualMove = Command::NONE;
      } else {
        // not moving manually
        pushCount++; // short press increase the count
      }

    }
    else
    { // released and idle
      // check timeout on release - indicating last press
      if ((lastPushTime != 0) && (currentMillis - lastPushTime > CLICK_TIMEOUT))
      {
        // last press
        if ((pushCount >= MIN_SLOT) && MEMORY_BUTTON(lastbutton))
          memoryEvent = true; // either store or recall a setting
        else { // single push or not a memory button.
          startFresh();
          // could still be a trigger for something. For now just...
#ifdef FEEDBACK
          // play short dull buzz, indicating this is a no-op.
          if (feedback) playTone(NOTE_A4, PIP_DURATION);
#endif
        }
      }
    }
  }

  previous = buttons;
}

#ifdef SERIALCOMMS

#ifdef HUMANSERIAL
int digits=0;

// interpret bytes as ascii digits, report when any non-digit is recieved
// preserve any partially decoded digits.
int readdigits()
{
  int r;
  while ((r = Serial1.read()) > 0) {
    if ((r < 0x30) || (r > 0x39)) {
      // non-digit we're done, return what we have
      return digits;
    }
    // it's a digit, add with base10 shift
    digits = 10*digits + (r-0x30);
    // keep reading...
  }
  return -1;
}

// human/ascii commands over serial.
// accepts commands like: <T2000,255\n  <=3000,. <C0.0.  <+200,0.  <L.2. <C.. <R,0 <R.34
// can safely accept 16 chars at a time before there's buffer overflow and loss.
// (the 2 numeric fields are terminated by any non-numeric character)
void recvData()
{
  const int numChars = 2;
  const int numFields = 4; // read/store all 4 fields for simplicity, use only the last 3.
  // static variables allows segmented/char-at-a-time decodes
  static uint16_t receivedBytes[numFields];
  static uint8_t ndx = 0;
  int r; // read char/digit

  // read 2 chars
  while ((ndx < numChars) && ((r = Serial1.read()) != -1))
  {
    if ((ndx == 0) && (r != rxMarker))
    {
      // first char is not Rx, keep reading...
      continue;
    }
    receivedBytes[ndx] = r;
    ++ndx;
  }
  // read ascii digits
  while ((ndx >= numChars) && ((r = readdigits()) != -1)) {
    receivedBytes[ndx] = r;
    digits = 0; // clear
    if (++ndx == numFields) {
      // thats all 4 fields. parse/process them now and break-out.
      parseData(receivedBytes[1],
                receivedBytes[2],
                receivedBytes[3]);
      ndx = 0;
      return;
    }
  }
}
#else
// modified from https://forum.arduino.cc/index.php?topic=396450.0
// read/process 5 raw bytes at a time or exit if no serial data available.
void recvData()
{
  const int numBytes = 5; // read/store all 5 bytes for simplicity, use only the last 4.
  // static variables allows segmented/char-at-a-time decodes
  static byte receivedBytes[numBytes];
  static uint8_t ndx = 0;
  int r; // read char

  while ((r = Serial1.read()) != -1)
  {
    if ((ndx == 0) && (r != rxMarker))
    {
      // first char is not rxMarker, keep reading...
      continue;
    }
    receivedBytes[ndx] = r;
    if (++ndx == numBytes)
    { // thats 5 now bytes, parse/process them now and break-out.
      parseData(receivedBytes[1],
                makeWord(receivedBytes[2], receivedBytes[3]),
                receivedBytes[4]);
      ndx = 0;
      return;
    }
  }
}
#endif

void writeSerial(byte operation, uint16_t position, uint8_t push_addr, byte marker)
{
  // note. serial.write only ever writes bytes. ints/longs get truncated!
  Serial1.write(marker);
  Serial1.write(operation);
#ifdef HUMANSERIAL
  Serial1.print(position); // Tx human-readable output option
  Serial1.print(',');
  Serial1.print(push_addr);
  Serial1.print('\n');
#else
  Serial1.write(position >> 8); // high byte
  Serial1.write(position & 0xff); // low byte
  Serial1.write(push_addr);
#endif
}

void parseData(byte command, uint16_t position, uint8_t push_addr)
{
#ifdef SERIALECHO
  writeSerial(command, position, push_addr, rxMarker); // echo command back for debugging
#endif
  /*
  data start (first byte)
    char   meaning
  -----------------
    <      start Rx Marker

  command (second byte)
    cmd    meaning
  -----------------
    +      increase
    -      decrease
    =      absolute
    C      Ask for current location
    S      Save location to (UpButton) EEPROM
    s      Save location to Downbutton EEPROM
    L      Load (Upbutton) location from EEPROM and move
    l      load Downbutton location from EEPROM and move
    W      Write arbitrary data to EEPROM
    R      Read arbitrary data from EEPROM
    T      play tone

  position (third/fourth bytes or 1st digit field. max 65535)
    cmd    meaning
  -----------------
    +-     relative to current position
    =SsW   absolute position
    T      tone frequency
    CRLl   (ignore)

  push_addr (fifth byte or 2nd digit field. max 255)
    cmd    meaning
  -----------------
    SLlWwR EEPROM pushCount/slot
    T      tone duration/4 ms. (250 == 1s)
    +-=C   (ignore)


  Transmitted operations
    char   meaning
  -----------------
    >      Tx start Marker
    E      error response
    X      calibration started
  */

  if (command == command_increase)
  {
    targetHeight = currentHeight + position;
    memoryMoving = true;
  }
  else if (command == command_decrease)
  {
    targetHeight = currentHeight - position;
    memoryMoving = true;
  }
  else if (command == command_absolute)
  {
    targetHeight = position;
    memoryMoving = true;
  }
  else if (command == command_current)
  {
    writeSerial(command_absolute, currentHeight);
  }
  else if ((command == command_save) || (command == command_save_down))
  {
    // note. saving against down-button requires adding 32 to push_addr
    // or by using command 's'
    if (command == command_save_down)
      push_addr += DOWN_SLOT_START;

    // if position not set, then use currentHeight
    if (position == 0)
    {
      position = currentHeight;
    }
    // save position to memory location
    saveMemory(push_addr, position);

#ifdef MINMAX
    // note. L command at same slot will *toggle* min/max heights.
    // S command writes it unconditionally. W writes without updating min/maxHeight.
    // Nowhere is there a check if limits are outside DANGER_MIN/MAX_HEIGHT...

    //if changing memory location for min/max height, update correct variable
    if (push_addr == MIN_HEIGHT_SLOT)
    {
      minHeight = position;
    }
    else if (push_addr == MAX_HEIGHT_SLOT)
    {
      maxHeight = position;
    }
#endif
  }
  else if (command == command_load)
  {
    // check if loading down-button slot
    if ((bothbuttons) && (push_addr > DOWN_SLOT_START)) {
      push_addr -= DOWN_SLOT_START;
      lastbutton = Button::DOWN;
    } else
      lastbutton = Button::UP;
    pushCount = push_addr;
    memoryEvent = true;
  }
  else if ((bothbuttons) && (command == command_load_down))
  {
    lastbutton = Button::DOWN;
    pushCount = push_addr;
    memoryEvent = true;
  }
  else if ((command == command_write) || (command == command_read))
  {
    if (command == command_write)
      eepromPut16(push_addr, position);

    writeSerial(command, eepromGet16(push_addr), push_addr);
  }
  else if (command == command_tone)
  {
    playTone(position, push_addr*4); // 255*4 ~ 1020ms max
  }
#ifdef SERIALERRORS
  else
  {
    // not a recognized command. writeSerial isn't very expressive for this error.
    // two options:
    //writeSerial(response_error, 0); // respond with a empty error
    // or
    // respond with the recieved message but with txMarker replaced with 'E'
    writeSerial(command, position, push_addr, response_error);
  }
#endif
}
#endif

void loop()
{
  readButtons();
#ifdef ENABLERESET
  if (memoryEvent && (pushCount == FORCE_RESET))
    softReset::Reset();
#endif

  // Wait before next cycle. 150ms on factory controller, 25ms seems fine.
  delayUntil(25);
  uint8_t drift = linBurst(); // drift is difference between enc_a and enc_b

  // If we are in recalibrate mode or have a bad currentHeight, don't act on input.
  if (state >= State::STARTING_RECAL || currentHeight <= 5)
  {
    return;
  }

#ifdef SERIALCOMMS
  if (memoryMoving == false && oldHeight != currentHeight){
    // report new location
    if (oldHeight < currentHeight){
      writeSerial(command_increase, currentHeight-oldHeight);
    }
    else {
      writeSerial(command_decrease, oldHeight-currentHeight);
    }
    writeSerial(command_absolute, currentHeight, drift);
    oldHeight = currentHeight;
  }
  recvData();
#endif

  if (memoryEvent)
  {
    if (pushCount == RECALIBRATE)
    {
      // do calibration
#ifdef SERIALCOMMS
      writeSerial(response_calibration, 0, pushCount);
#endif
      state = State::STARTING_RECAL;
    }
#ifdef MINMAX
    else if (pushCount == MIN_HEIGHT_SLOT)
    {
      toggleMinHeight();
    }
    else if (pushCount == MAX_HEIGHT_SLOT)
    {
      if (ADJUST_DOWN) // 12 presses on either button sets corresponding max/min limits
        toggleMinHeight();
      else
        toggleMaxHeight();
    }
#endif
    else if (pushCount == BOTHBUTTON_SLOT)
    {
      toggleBothMode();
    }
#ifdef FEEDBACK
    else if (pushCount == FEEDBACK_SLOT)
    {
      toggleFeedback();
    }
#endif
    else if (savePosition)
    {
      if (ADJUST_DOWN)
        pushCount += DOWN_SLOT_START;
      saveMemory(pushCount, currentHeight);
    }
    else
    {
      // load position
      beep(NOTE_LOW, pushCount);
      if (ADJUST_DOWN)
        pushCount += DOWN_SLOT_START;
      targetHeight = loadMemory(pushCount);
      memoryMoving = true;
    }
    startFresh(); // clears memoryEvent, pushCount, lastPushTime
  }
  else if (manualMove == Command::UP)
  {
    memoryMoving = false;
    // max() allows escape from recalibration (below DANGER_MIN_HEIGHT)
    targetHeight = max(currentHeight + HYSTERESIS + 1, DANGER_MIN_HEIGHT);
  }
  else if (manualMove == Command::DOWN)
  {
    memoryMoving = false;
    // min() allows descend if beyond DANGER_MAX_HEIGHT
    targetHeight = min(currentHeight - HYSTERESIS - 1, DANGER_MAX_HEIGHT);
  }
  else if (!memoryMoving)
  {
    // prevent hunting if overshot target. call it quits if shy.
    targetHeight = currentHeight;
  }

  // avoid moving toward an out-of-bounds position
  if ((targetHeight < DANGER_MIN_HEIGHT) || (targetHeight > DANGER_MAX_HEIGHT)) {
#if (defined SERIALCOMMS && defined SERIALERRORS)
    writeSerial(response_error, targetHeight); // Indicate an error and the bad targetHeight
#endif
    targetHeight = currentHeight; // abandon target
  }

  // Turn on motors?
  if (targetHeight > currentHeight + HYSTERESIS && currentHeight < maxHeight)
    MOTOR_UP;
  else if (targetHeight < currentHeight - HYSTERESIS && currentHeight > minHeight)
    MOTOR_DOWN;
  else
  {
    // some possibilities:
    //   close enough and still coasting,
    //   or overshot,
    //   or out of range!
    // so stop
    MOTOR_OFF;
    memoryMoving = false;
  }

}

uint8_t linBurst()
{
  static byte empty[3] = {0, 0, 0};
  static byte cmd[3] = {0, 0, 0};
  static byte node_a[4] = {0, 0, 0, 0};
  static byte node_b[4] = {0, 0, 0, 0};
  static State lastState = State::OFF;

  // Send PID 17
  lin.send(17, empty, 3);

  delayUntil(5);
  // Recv from PID 09
#ifdef DEBUG_ENCODER
  uint8_t chars = lin.recv(9, node_b, 3);
  if (chars != 4)
    writeSerial(node_b[1], (node_b[0]<<8) + node_b[2], 1, chars);
#else
  lin.recv(9, node_b, 3);
#endif

  delayUntil(5);
  // Recv from PID 08
#ifdef DEBUG_ENCODER
  chars = lin.recv(8, node_a, 3);
  if (chars != 4)
    writeSerial(node_a[1], (node_a[0]<<8) + node_a[2], 0, chars);
#else
  lin.recv(8, node_a, 3);
#endif

  // Send PID 16, 6 times
  for (byte i = 0; i < 6; i++)
  {
    delayUntil(5);
    lin.send(16, 0, 0);
  }

  delayUntil(5);
  // Send PID 1
  lin.send(1, 0, 0);

  uint16_t enc_a = node_a[0] | (node_a[1] << 8);
  uint16_t enc_b = node_b[0] | (node_b[1] << 8);
  uint16_t enc_target = enc_a;
  currentHeight = enc_a;
#ifdef DEBUG_ENCODER
  if (((enc_a>enc_b) && (enc_a-enc_b>20)) ||
      ((enc_b>enc_a) && (enc_b-enc_a>20)))
    writeSerial(node_a[1], (node_a[0]<<8) + node_b[1], node_b[0], 0xAA);
#endif

  // Send PID 18
  switch (state)
  {
  case State::OFF:
    cmd[2] = LIN_CMD_IDLE;
    break;

  case State::STARTING:
  case State::STARTING_RECAL:
    cmd[2] = LIN_CMD_PREMOVE;
    break;

  case State::UP:
    enc_target = min(enc_a, enc_b);
    cmd[2] = LIN_CMD_RAISE;
    lastState = State::UP;
    break;

  case State::DOWN:
    enc_target = max(enc_a, enc_b);
    cmd[2] = LIN_CMD_LOWER;
    lastState = State::DOWN;
    break;

  case State::STOPPING1:
  case State::STOPPING2:
  case State::STOPPING3:
    if (lastState == State::UP)
      enc_target = min(enc_a, enc_b) + FINE_MOVEMENT_VALUE;
    else
      enc_target = min(enc_a, enc_b) - FINE_MOVEMENT_VALUE;

    cmd[2] = LIN_CMD_FINE;
    break;

  case State::STOPPING4:
    enc_target = max(enc_a, enc_b);
    cmd[2] = LIN_CMD_FINISH;
    break;

  case State::RECAL:
    // We want to send 0/0/189
    cmd[2] = LIN_CMD_RECALIBRATE;
    enc_target = 0; 
    break;
  case State::END_RECAL:
    // We want to send 99/0/188
    cmd[2] = LIN_CMD_RECALIBRATE_END;
    enc_target = 99;
    break;
  }

  cmd[0] = enc_target & 0xFF;
  cmd[1] = enc_target >> 8;
  delayUntil(5);
  lin.send(18, cmd, 3);

  switch (state)
  {
  case State::OFF:
    if (user_cmd != Command::NONE)
    {
      if (node_a[2] == node_b[2])
      {
        if (node_a[2] == LIN_MOTOR_IDLE1 || node_a[2] == LIN_MOTOR_IDLE2 || node_a[2] == LIN_MOTOR_IDLE3)
        {
          state = State::STARTING;
#if (defined SERIALCOMMS && defined SERIAL_IDLE)
          writeSerial(response_idle, 0, node_a[2]); // Indicate the idle variant type
#endif
        }
      }
    }
    break;
  case State::STARTING:
    switch (user_cmd)
    {
    case Command::NONE:
      state = State::OFF;
      break;
    case Command::UP:
      state = State::UP;
      break;
    case Command::DOWN:
      state = State::DOWN;
      break;
    }
    break;
  case State::UP:
    if (user_cmd != Command::UP || currentHeight >= maxHeight)
    {
      state = State::STOPPING1;
    }
    break;
  case State::DOWN:
    if (user_cmd != Command::DOWN || currentHeight <= minHeight)
    {
      state = State::STOPPING1;
    }
    break;
  case State::STOPPING1:
    state = State::STOPPING2;
    break;
  case State::STOPPING2:
    state = State::STOPPING3;
    break;
  case State::STOPPING3:
    state = State::STOPPING4;
    break;
  case State::STOPPING4:
    if (node_a[2] == node_b[2])
    {
      if (node_a[2] == LIN_MOTOR_IDLE1 || node_a[2] == LIN_MOTOR_IDLE2 || node_a[2] == LIN_MOTOR_IDLE3)
      {
        state = State::OFF;
      }
    }
    break;
  // recal stuff here
  case State::STARTING_RECAL:
    state = State::RECAL;
    break;
  case State::RECAL:
    if (node_a[0] <= 99 && node_a[1] == 0 && node_a[2] == 1 &&
        node_b[0] <= 99 && node_b[1] == 0 && node_b[2] == 1) // Are both motors reporting 99/0/1? We should be bottomed out at this point.
        state = State::END_RECAL;
    break;
  case State::END_RECAL:
    state = State::OFF;
    break;
  default:
    state = State::OFF;
    break;
  }
  return ((enc_b>enc_a)?enc_b-enc_a:enc_a-enc_b);
}

// lean EEPROM functions to get/put 16bit values
// and address eeprom by 16bit slot.
uint16_t eepromGet16( int slot )
{
  return ((EEPROM.read( 2*slot+1 ) << 8) | EEPROM.read( 2*slot ));
}

void eepromPut16( int slot, uint16_t val )
{
  EEPROM.write( 2*slot, val & 0xff);
  EEPROM.write( 2*slot+1, val >> 8);
}

void saveMemory(uint8_t memorySlot, uint16_t value)
{
  // Sanity check
  if (memorySlot < MIN_SLOT || value < DANGER_MIN_HEIGHT || value > DANGER_MAX_HEIGHT) {
#if (defined SERIALCOMMS && defined SERIALERRORS)
    writeSerial(response_error, value, memorySlot); // Indicate an error
#endif
    return;
  }
  // save confirmation tone
  beep(NOTE_HIGH);

#ifdef SERIALCOMMS
  if (memorySlot > DOWN_SLOT_START)
    writeSerial(command_save_down, value, memorySlot-DOWN_SLOT_START);
  else
    writeSerial(command_save, value, memorySlot);
#endif

  eepromPut16(memorySlot, value);
}

uint16_t loadMemory(uint8_t memorySlot)
{
  if (memorySlot < MIN_SLOT) {
#if (defined SERIALCOMMS && defined SERIALERRORS)
    writeSerial(response_error, 0, memorySlot); // Indicate an error
#endif
    return currentHeight;
  }
  uint16_t memHeight = eepromGet16(memorySlot);

  if (memHeight == 0)
  {
    // empty
    delay(LONG_PAUSE);
    // sad trombone
    beep(NOTE_DSHARP6);
    beep(NOTE_D6);
    beep(NOTE_CSHARP6);
    beep(NOTE_C6);
#if (defined SERIALCOMMS && defined SERIALERRORS)
    writeSerial(response_error, 0, memorySlot); // Indicate an error
#endif
    return currentHeight;
  }

#ifdef SERIALCOMMS
  if (memorySlot > DOWN_SLOT_START)
    writeSerial(command_load_down, memHeight, memorySlot-DOWN_SLOT_START);
  else
    writeSerial(command_load, memHeight, memorySlot);
#endif

  return memHeight;
}


// accurate delay from the last time delayUntil() returned.
// for precise periodic timing
void delayUntil(uint16_t milliSeconds)
{
  unsigned long target = refTime + (1000 * milliSeconds);
  unsigned long micro_delay = target - micros();

  if (micro_delay > 1000000)
  {
    // >1s - long delay - target time is in the past!
#if (defined SERIALCOMMS && defined SERIALERRORS)
    // report lateness (us) and requested delay (ms)
    writeSerial(response_error, (-micro_delay), milliSeconds, lateMarker);
#endif
    // reset refTime and return
    refTime = micros();
    return;
  }

  if (micro_delay > 15000)
  {
    // need delayMicroseconds() and delay()
    unsigned long millidelay = (micro_delay - 15000) / 1000;
    delay(millidelay);
    // recalculate microsec delay
    micro_delay = target - micros();
  }
  delayMicroseconds(micro_delay);
  refTime = target;
}

// simple, limited tone generation - leaner than tone()
// but sound will break up if servicing interrupts (like receiving serial).
// freq is in Hz. duration is in ms. (max 1048ms)
void playTone(uint16_t freq, uint16_t duration) {
  uint16_t halfperiod = 500000L / freq; // in us.
  // mostly equivalent to:
  // for (long i = 0; i < duration * 1000L; i += halfperiod * 2) {
  for ( duration = (62 * duration) / (halfperiod/8); duration > 0; duration-- ) {
    digitalWrite(PIN_BEEP, HIGH);
    delayMicroseconds(halfperiod - PITCH_ADJUST);
    digitalWrite(PIN_BEEP, LOW);
    delayMicroseconds(halfperiod - PITCH_ADJUST);
  }
}

void beep(uint16_t freq, uint8_t count)
{
  // functionally equivalent to:
  // for (uint8_t i = 0; i < count; i++)
  for ( ; count > 0; count--)
  {
    playTone(freq, BEEP_DURATION);
    delay(SHORT_PAUSE);
  }
}

uint8_t initFailures = 0;

void sendInitPacket(byte a1, byte a2, byte a3, byte a4)
{
  static byte packet[8] = {0, 0, 0, 0, 255, 255, 255, 255};
  packet[0] = a1;
  packet[1] = a2;
  packet[2] = a3;
  packet[3] = a4;
#ifdef DEBUGSTARTUP
  writeSerial(a1, (a2<<8)+a3, a4, 8);
#endif
  delayUntil(10); // long frames need more time
  lin.send(60, packet, 8);
}

byte recvInitPacket()
{
  static byte resp[8];
  delayUntil(10); // long frames need more time
#ifdef DEBUGSTARTUP
  uint8_t chars= lin.recv(61, resp, 8);
  if ((chars !=0) && (chars <0xF0))
    writeSerial(resp[0], (resp[1]<<8) + resp[2], resp[3], chars);
  else
    writeSerial(5, (5<<8)+5, 5, chars);
  if (chars == 0xfd)
    initFailures++;
  return chars;
#else
  return lin.recv(61, resp, 8);
#endif
}

#ifdef SMALL_INIT

#define SEQUENCE_LENGTH 21
const byte init_seq[SEQUENCE_LENGTH][4] = {
  {255, 7, 255, 255}, //0 (no resp)
  {255, 7, 255, 255}, //1 (no resp)
  {255, 1, 7, 255}, //2 (no resp)
  {208, 2, 7, 255}, //3
#define REPEAT1 4
  {0, 2, 7, 255}, //4 REPEAT1 until resp >0
  {0, 6, 9, 0}, //5
  {0, 6, 12, 0}, //6
  {0, 6, 13, 0}, //7
  {0, 6, 10, 0}, //8
  {0, 6, 11, 0}, //9
  {0, 4, 0, 0}, //10
#define REPEAT2 11
  {0, 2, 0, 0}, //11 REPEAT2 until resp>0
  {0, 6, 9, 0}, //12
  {0, 6, 12, 0}, //13
  {0, 6, 13, 0}, //14
  {0, 6, 10, 0}, //15
  {0, 6, 11, 0}, //16
  {0, 4, 1, 0}, //17
#define REPEAT3 18
  {0, 2, 1, 0}, //18 REPEAT8 while seq <8 (no resp)

  {208, 1, 7, 0}, //19 (no resp)
  {208, 2, 7, 0}, //20 (no resp)
};

void linInit()
{
  static const byte magicPacket[3] = {246, 255, 191};
  // Really weird startup sequence, sourced from the controller.

  // Brief stabilization delay
  delay(250);
  int8_t initA = -1;

  refTime = micros();
  for (uint8_t i=0; i<SEQUENCE_LENGTH; i++) {
    if ((i==REPEAT1) || (i==REPEAT2)) {
      while (initA < 8)
      {
        initA++;
        sendInitPacket(initA, init_seq[i][1], init_seq[i][2], init_seq[i][3]);
        if (recvInitPacket() > 0) break;
      }
      if (initA >= 8) initFailures++;
    } else if (i==REPEAT3) {
      while (initA < 8)
      {
        initA++;
        sendInitPacket(initA, init_seq[i][1], init_seq[i][2], init_seq[i][3]);
        recvInitPacket(); // no response expected
      }
    } else {
      if (init_seq[i][0])
        sendInitPacket(init_seq[i][0], init_seq[i][1], init_seq[i][2], init_seq[i][3]);
      else
        sendInitPacket(initA, init_seq[i][1], init_seq[i][2], init_seq[i][3]);
      recvInitPacket();
    }
  }

  delayUntil(15);
  lin.send(18, magicPacket, 3);

  delay(5);
  if (initFailures) {
#if (defined SERIALCOMMS && defined SERIALERRORS)
    // report boot failure. and how many legs failed (initFailures)
    writeSerial(response_error, 0, initFailures, bootfailMarker);
#endif
#ifdef ENABLERESET
    softReset::Reset();
#endif
  }
}

#else
void linInit()
{
  // Really weird startup sequenced, sourced from the controller.
  static const byte magicPacket[3] = {246, 255, 191};

  // Brief stabilization delay
  delay(150);
  refTime = micros();

  sendInitPacket(255, 7);
  recvInitPacket();

  sendInitPacket(255, 7);
  recvInitPacket();

  sendInitPacket(255, 1, 7);
  recvInitPacket();

  sendInitPacket(208, 2, 7);
  recvInitPacket();

  uint8_t initA = 0;
  while (true)
  {
    sendInitPacket(initA, 2, 7);
    if (recvInitPacket() > 0)
      break;
    initA++;
  }

  sendInitPacket(initA, 6, 9, 0);
  recvInitPacket();

  sendInitPacket(initA, 6, 12, 0);
  recvInitPacket();

  sendInitPacket(initA, 6, 13, 0);
  recvInitPacket();

  sendInitPacket(initA, 6, 10, 0);
  recvInitPacket();

  sendInitPacket(initA, 6, 11, 0);
  recvInitPacket();

  sendInitPacket(initA, 4, 0, 0);
  recvInitPacket();

  byte initB = initA + 1;
  while (true)
  {
    sendInitPacket(initB, 2, 0, 0);
    if (recvInitPacket() > 0)
      break;
    initB++;
  }

  sendInitPacket(initB, 6, 9, 0);
  recvInitPacket();

  sendInitPacket(initB, 6, 12, 0);
  recvInitPacket();

  sendInitPacket(initB, 6, 13, 0);
  recvInitPacket();

  sendInitPacket(initB, 6, 10, 0);
  recvInitPacket();

  sendInitPacket(initB, 6, 11, 0);
  recvInitPacket();

  sendInitPacket(initB, 4, 1, 0);
  recvInitPacket();

  uint8_t initC = initB + 1;
  while (initC < 8)
  {
    sendInitPacket(initC, 2, 1, 0);
    recvInitPacket();
    initC++;
  }

  sendInitPacket(208, 1, 7, 0);
  recvInitPacket();

  sendInitPacket(208, 2, 7, 0);
  recvInitPacket();

  delayUntil(15);
  lin.send(18, magicPacket, 3);

  delay(5);
}
#endif

void initAndReadEEPROM(bool force)
{
  int signature = eepromGet16(EEPROM_SIG_SLOT);

  if ((signature != MAGIC_SIG) || force)
  {
    // use 8bit wraparound as exit-condition
    for (uint8_t index = 2; index != 0; index++) {
      eepromPut16(index,0);
      // 2nd half of eeprom could be a back-up of the 1st half...
      // then swap/copy between the two (for different users? backups?)
    }
    // Store signature value
    eepromPut16(EEPROM_SIG_SLOT, MAGIC_SIG);

  }
#ifdef MINMAX
  // retrieve max/min height
  minHeight = eepromGet16(MIN_HEIGHT_SLOT);
  maxHeight = eepromGet16(MAX_HEIGHT_SLOT);
  // check if invalid and fix/beep ...
  if (minHeight == 0) toggleMinHeight();
  if (maxHeight == 0) toggleMaxHeight();
#endif
  bothbuttons = eepromGet16(BOTHBUTTON_SLOT);
#ifdef FEEDBACK
  feedback = eepromGet16(FEEDBACK_SLOT);
#endif
}


#ifdef MINMAX
// Swap the minHeight values and save in EEPROM
void toggleMinHeight()
{
  // no bounds checks for < DANGER_MIN_HEIGHT!

  if (minHeight == DANGER_MIN_HEIGHT)
  {
    minHeight = currentHeight;
    // provide more feedback when setting than clearing
    beep(minHeight, 4);
  }
  else
  {
    minHeight = DANGER_MIN_HEIGHT;
    // default-limits sound
    beep(minHeight);
  }
  
  eepromPut16(MIN_HEIGHT_SLOT, minHeight);
}

// Swap the maxHeight values and save in EEPROM
void toggleMaxHeight()
{
  // no bounds checks for > DANGER_MAX_HEIGHT!

  if (maxHeight == DANGER_MAX_HEIGHT)
  {
    maxHeight = currentHeight;
    // provide more feedback when setting than clearing
    beep(maxHeight, 4);
  }
  else
  {
    maxHeight = DANGER_MAX_HEIGHT;
    // default-limits sound
    beep(maxHeight);
  }

  eepromPut16(MAX_HEIGHT_SLOT, maxHeight);
}
#endif

void toggleBothMode()
{
  bothbuttons = !bothbuttons;
  eepromPut16(BOTHBUTTON_SLOT, bothbuttons);
  beep(NOTE_C7, bothbuttons+1);
}

#ifdef FEEDBACK
void toggleFeedback()
{
  feedback = !feedback;
  eepromPut16(FEEDBACK_SLOT, feedback);
  beep(NOTE_C6, feedback+1);
}
#endif