// Uncomment this define if you want serial
#define SERIALCOMMS

// Uncomment this if you want to override minimum/maximum heights
#define MINMAX

// option to turn on immediate user feedback pips
#define FEEDBACK

// Transmit ascii values over serial for human-readable output
//#define HUMANSERIAL

// easter egg
#define EASTER

#include <EEPROM.h>
#include "lin.h"
#include "megadesk.h"

#define HYSTERESIS 137
#define PIN_UP 10
#define PIN_DOWN 9
#define PIN_BEEP 7
#define PIN_SERIAL 1

// beeps
#define BEEP_DURATION 150
#define PIP_DURATION 20
#define SHORT_PAUSE 50
#define PAUSE BEEP_DURATION
#define LONG_PAUSE 500
#define ONE_SEC_PAUSE 1000
#define PITCH_ADJUST (48000000 / F_CPU) // digitalWrite takes ~6us at 8MHz

// notes to beep
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

#define CLICK_TIMEOUT 400UL // Timeout in MS.
#define CLICK_LONG    900UL    // Long/hold minimum time in MS.

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
#define DANGER_MAX_HEIGHT 6777 - HYSTERESIS
#define DANGER_MIN_HEIGHT 162 + HYSTERESIS

// constants related to presses/eeprom slots
// (on attiny841: 512byte eeprom means max 255 slots)
// EEPROM magic signature to detect if eeprom is valid
#define EEPROM_SIG_SLOT  0
#define MAGIC_SIG 0x120d // bytes: 13, 18 in little endian order
#define MIN_HEIGHT_SLOT  11
#define MAX_HEIGHT_SLOT  12
#define RECALIBRATE      14 // nothing is stored there
#define RESERVED_VARIANT 16 // reserved - deliberately empty
#define FEEDBACK_SLOT    17 // short tones on every button-press. buzz on no-ops
#define BOTHBUTTON_SLOT  18 // store whether bothbuttons is enabled
#define DOWN_SLOT_START  32 // 0x20 in hex offset for down button slots

// any button pressed
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
Command user_cmd = Command::NONE;
State state = State::OFF;

#ifdef SERIALCOMMS
uint16_t oldHeight = 0; // previously reported height
const char startMarker = '<';
const char command_increase = '+';
const char command_decrease = '-';
const char command_absolute = '=';
const char command_write = 'W';
const char command_load = 'L';
const char command_current = 'C';
const char command_read = 'R';
const char command_tone = 'T';
#endif

// set/clear motor up command
#define MOTOR_UP user_cmd = Command::UP
#define MOTOR_DOWN user_cmd = Command::DOWN
#define MOTOR_OFF user_cmd = Command::NONE

// clean the slate for button presses
void startFresh()
{
  previous = Button::NONE;
  lastPushTime = 0;
  pushCount = 0;
  savePosition = false;
  memoryEvent = false;
}

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
    beep(NOTE_C7);
    beep(NOTE_C7);
    delay(LONG_PAUSE);
    while (true)
    {
      if (!digitalRead(PIN_DOWN))
      {
        beep(NOTE_E7);
        beep(NOTE_D7);
        beep(NOTE_C7);
        delay(LONG_PAUSE);
      }
      if (!digitalRead(PIN_UP))
      {
        beep(NOTE_C7);
        beep(NOTE_D7);
        beep(NOTE_E7);
        delay(LONG_PAUSE);
      }
      delay(SHORT_PAUSE);
    }

  }

#ifdef SERIALCOMMS
  Serial1.begin(19200);
#endif

  // init + arpeggio
  beep(NOTE_C6);

  initAndReadEEPROM(false);
  beep(NOTE_E6);

  lin.begin(19200);
  beep(NOTE_G6);

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

  // If already moving and any button is pressed - stop!
  if ( memoryMoving && PRESSED(buttons))
    targetHeight = currentHeight;


  if ( (previous != buttons) && PRESSED(buttons) ) // new push
  {
    // clear pushCount if pushing a different button from last
    if (buttons != lastbutton)
      startFresh();

    lastPushTime = currentMillis;
    lastbutton = buttons;
#ifdef FEEDBACK
    if (feedback)
      playTone(scale[pushCount % (sizeof(scale)/sizeof(int))],
              PIP_DURATION); // musical feedback
#endif
  }
  else if ((previous == buttons) && PRESSED(buttons) ) // button held
  {
    unsigned long pushLength = currentMillis - lastPushTime;

    // long push?
    if (pushLength > CLICK_TIMEOUT) {
      // first long push? then move!
      if (pushCount == 0) {
        if (buttons == Button::UP) manualMove = Command::UP;
        if (buttons == Button::DOWN) manualMove = Command::DOWN;
#ifdef EASTER
        if ((buttons == Button::BOTH) && (pushLength > 25*CLICK_TIMEOUT)) {
          // 10s hold. unused trigger, play the easter-egg
         #define EIGHTH 131
          uint16_t tones[] = {
            NOTE_C6, EIGHTH*2, NOTE_C7, EIGHTH*8,
            NOTE_B6, EIGHTH, NOTE_C7, EIGHTH,
            NOTE_B6, EIGHTH, NOTE_G6, EIGHTH,
            NOTE_A6, EIGHTH*8,
            NOTE_F6, EIGHTH, 20, EIGHTH,
            NOTE_F6, EIGHTH*2,
            NOTE_F6, EIGHTH, NOTE_E6, EIGHTH,
            NOTE_D6, EIGHTH, NOTE_E6, EIGHTH,
            NOTE_C6, EIGHTH, 20, EIGHTH, };
          for (uint16_t i=0; i < sizeof(tones)/2; i+=2) {
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
  else if ( PRESSED(previous) && !PRESSED(buttons) ) // just released
  {
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
  else if ( !PRESSED(previous) && !PRESSED(buttons) ) // released and idle
  {
    // check timeout on release - indicating last press
    if ((lastPushTime != 0) && (currentMillis - lastPushTime > CLICK_TIMEOUT))
    {
      // last press
      if ((pushCount > 1) && MEMORY_BUTTON(lastbutton))
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

  previous = buttons;
}

// modified from https://forum.arduino.cc/index.php?topic=396450.0
#ifdef SERIALCOMMS
// read/process 5 bytes at a time or exit if no serial data available.
void recvData()
{
  const int numBytes = 5; // read/store all 5 bytes for simplicity, use only the last 4.
  // static variables allows segmented/char-at-a-time decodes
  static byte receivedBytes[numBytes];
  static uint8_t ndx = 0;
  int r; // read char

  while ((r = Serial1.read()) != -1)
  {
    if ((ndx == 0) && (r != startMarker))
    {
      // first char is not the startMarker, keep reading...
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

void writeSerial(byte operation, uint16_t position, uint8_t push_addr)
{
  // note. serial.write only ever writes bytes. ints/longs get truncated!
  Serial1.write((byte) startMarker);
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
  //writeSerial(command, position, push_addr); // echo command back for debugging
  /*
  data start (first byte)
    <    start data sentence

  command (second byte)
    +    increase
    -    decrease
    =    absolute
    C    Ask for current location
    W    Write EEPROM location
    R    Read EEPROM location
    L    Load EEPROM location
    T    play tone

  position (third/fourth bytes)
    +-   relative to current
    =W   absolute
    T    tone frequency
    CRL  (ignore)

  push_addr (fifth byte)
    WRL   EEPROM pushCount number
    T     tone duration/4 ms. (250 == 1s)
    *     (ignore)
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
  else if (command == command_write)
  {
    // note. saving against down-button requires adding 32 to push_addr...

    // if position not set, then set to currentHeight
    if (position == 0)
    {
      position = currentHeight;
    }

    // save position to memory location
    saveMemory(push_addr, position);

#ifdef MINMAX
    //if changing memory location for min/max height, update corect variable
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
    // note. loading down-button slots requires adding 32 to push_addr...

    pushCount = push_addr;
    memoryEvent = true;
  }
  else if (command == command_read)
  {
    writeSerial(command_read, eepromGet16(push_addr), push_addr);
  }
  else if (command == command_tone)
  {
    playTone(position, push_addr*4); // 256*4 ~ 1048ms max
  }
}
#endif

void loop()
{

  linBurst();

  // If we are in recalibrate mode, don't respond to any inputs.
  if (state == State::STARTING_RECAL || state == State::RECAL || state == State::END_RECAL)
  {
    delayUntil(25);
    return;
  }

#ifdef SERIALCOMMS
  if (memoryMoving == false && oldHeight != currentHeight){
    if (oldHeight < currentHeight){
      writeSerial(command_increase, currentHeight-oldHeight, pushCount);
    }
    else {
      writeSerial(command_decrease, oldHeight-currentHeight, pushCount);
    }
  }

  recvData();
#endif

  readButtons();

  // When we power on the first time, and have a height value read, set our target height to the same thing
  // So we don't randomly move on powerup.
  if (currentHeight > 5 && targetHeight == 0)
  {
    targetHeight = currentHeight;
  }

  if (memoryEvent)
  {
    if (pushCount == RECALIBRATE)
    {
      // do calibration
      state = State::STARTING_RECAL;
    }
#ifdef MINMAX
    else if (pushCount == MIN_HEIGHT_SLOT)
    {
      toggleMinHeight();
    }
    else if (pushCount == MAX_HEIGHT_SLOT)
    {
      if (ADJUST_DOWN)
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
#ifdef SERIALCOMMS
      writeSerial(command_write, currentHeight, pushCount);
#endif
    }
    else
    {
      // load position
      beep(NOTE_LOW, pushCount);
      if (ADJUST_DOWN)
        pushCount += DOWN_SLOT_START;
      targetHeight = loadMemory(pushCount);
#ifdef SERIALCOMMS
      writeSerial(command_load, targetHeight, pushCount);
#endif

      if (targetHeight == 0)
      {
        targetHeight = currentHeight;
      }
      else
        memoryMoving = true;
    }
    startFresh(); // clears memoryEvent, pushCount, lastPushTime
  }
  else if (manualMove == Command::UP)
  {
    memoryMoving = false;
    targetHeight = currentHeight + HYSTERESIS + 1;
  }
  else if (manualMove == Command::DOWN)
  {
    memoryMoving = false;
    targetHeight = currentHeight - HYSTERESIS - 1;
  }
  else if (!memoryMoving){
#ifdef SERIALCOMMS
    if (oldHeight != currentHeight){
      writeSerial(command_absolute, currentHeight);
    }
#endif
    targetHeight = currentHeight;
  }

  if (targetHeight > currentHeight && abs(targetHeight - currentHeight) > HYSTERESIS && currentHeight < maxHeight)
    MOTOR_UP;
  else if (targetHeight < currentHeight && abs(targetHeight - currentHeight) > HYSTERESIS && currentHeight > minHeight)
    MOTOR_DOWN;
  else
  {
    MOTOR_OFF;
    targetHeight = currentHeight;
    memoryMoving = false;
  }

  // Override all logic above and disable if we aren't initialized yet.
  if (targetHeight < 5)
    MOTOR_OFF;

#ifdef SERIALCOMMS
  oldHeight = currentHeight;
#endif

  // Wait before next cycle. 150ms on factory controller, 25ms seems fine.
  delayUntil(25);
}

void linBurst()
{
  byte node_a[4] = {0, 0, 0, 0};
  byte node_b[4] = {0, 0, 0, 0};
  byte cmd[3] = {0, 0, 0};
  static State lastState = State::OFF;

  // ensure accurate timing from this point
  refTime = micros();

  // Send PID 17
  lin.send(17, empty, 3, 2);
  delayUntil(5);

  // Recv from PID 09
  lin.recv(9, node_b, 3, 2);
  delayUntil(5);

  // Recv from PID 08
  lin.recv(8, node_a, 3, 2);
  delayUntil(5);

  // Send PID 16, 6 times
  for (byte i = 0; i < 6; i++)
  {
    lin.send(16, 0, 0, 2);
    delayUntil(5);
  }

  // Send PID 1
  lin.send(1, 0, 0, 2);
  delayUntil(5);

  uint16_t enc_a = node_a[0] | (node_a[1] << 8);
  uint16_t enc_b = node_b[0] | (node_b[1] << 8);
  uint16_t enc_target = enc_a;
  currentHeight = enc_a;

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
  lin.send(18, cmd, 3, 2);

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
    if (node_a[0] == 99 && node_a[1] == 0 && node_a[2] == 1 &&
        node_b[0] == 99 && node_b[1] == 0 && node_b[2] == 1) // Are both motors reporting 99/0/1? We should be bottomed out at this point.
        state = State::END_RECAL;
    break;
  case State::END_RECAL:
    state = State::OFF;
    break;
  default:
    state = State::OFF;
    break;
  }
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
  if (memorySlot < 2 || value < 5 || value > 32700)
    return;

  // save confirmation tone
  beep(NOTE_HIGH);

  eepromPut16(memorySlot, value);
}

uint16_t loadMemory(uint8_t memorySlot)
{
  if (memorySlot < 2)
    return currentHeight;

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
  }

  return memHeight;
}


// accurate delay from the last time delayUntil() returned.
// for precise periodic timing
void delayUntil(unsigned long microSeconds)
{
  unsigned long target = refTime + (1000 * microSeconds);
  unsigned long micro_delay = target - micros();

  if (micro_delay > 1000000)
  {
    // crazy long delay - target time is in the past!
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
// but sound will break up if servicing interrupts.
// freq is in Hz. duration is in ms. (max 1048ms)
void playTone(uint16_t freq, uint16_t duration) {
  uint16_t halfperiod = 500000L / freq; // in us.
  // mostly equivalent to:
  // for (long i = 0; i < duration * 1000L; i += halfperiod * 2) {
  for ( duration = (62 * duration) / (halfperiod / 8); duration > 0; duration -= 1 ) {
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

void sendInitPacket(byte a1, byte a2, byte a3, byte a4)
{
  byte packet[8] = {a1, a2, a3, a4, 255, 255, 255, 255};

  // Custom checksum formula for the initialization
  int chksum = a1 + a2 + a3 + a4;
  chksum = chksum % 255;
  chksum = 255 - chksum;

  lin.send(60, packet, 8, 2, chksum);
  delay(3);
}

void linInit()
{
  // Really weird startup sequenced, sourced from the controller.
  byte resp[8];

  // Brief stabilization delay
  delay(150);

  sendInitPacket(255, 7);
  recvInitPacket(resp);

  sendInitPacket(255, 7);
  recvInitPacket(resp);

  sendInitPacket(255, 1, 7);
  recvInitPacket(resp);

  sendInitPacket(208, 2, 7);
  recvInitPacket(resp);

  uint8_t initA = 0;
  while (true)
  {
    sendInitPacket(initA, 2, 7);
    if (recvInitPacket(resp) > 0)
      break;
    initA++;
  }

  sendInitPacket(initA, 6, 9, 0);
  recvInitPacket(resp);

  sendInitPacket(initA, 6, 12, 0);
  recvInitPacket(resp);

  sendInitPacket(initA, 6, 13, 0);
  recvInitPacket(resp);

  sendInitPacket(initA, 6, 10, 0);
  recvInitPacket(resp);

  sendInitPacket(initA, 6, 11, 0);
  recvInitPacket(resp);

  sendInitPacket(initA, 4, 0, 0);
  recvInitPacket(resp);

  byte initB = initA + 1;
  while (true)
  {
    sendInitPacket(initB, 2, 0, 0);
    if (recvInitPacket(resp) > 0)
      break;
    initB++;
  }

  sendInitPacket(initB, 6, 9, 0);
  recvInitPacket(resp);

  sendInitPacket(initB, 6, 12, 0);
  recvInitPacket(resp);

  sendInitPacket(initB, 6, 13, 0);
  recvInitPacket(resp);

  sendInitPacket(initB, 6, 10, 0);
  recvInitPacket(resp);

  sendInitPacket(initB, 6, 11, 0);
  recvInitPacket(resp);

  sendInitPacket(initB, 4, 1, 0);
  recvInitPacket(resp);

  uint8_t initC = initB + 1;
  while (initC < 8)
  {
    sendInitPacket(initC, 2, 1, 0);
    recvInitPacket(resp);
    initC++;
  }

  sendInitPacket(208, 1, 7, 0);
  recvInitPacket(resp);

  sendInitPacket(208, 2, 7, 0);
  recvInitPacket(resp);

  delay(15);

  byte magicPacket[3] = {246, 255, 191};
  lin.send(18, magicPacket, 3, 2);

  delay(5);
}

byte recvInitPacket(byte array[])
{
  return lin.recv(61, array, 8, 2);
}

void initAndReadEEPROM(bool force)
{
  int signature = eepromGet16(EEPROM_SIG_SLOT);

  if ((signature != MAGIC_SIG) || force)
  {
    for (unsigned int index = 0; index < EEPROM.length(); index++)
      EEPROM.write(index, 0);
    // Store signature value
    eepromPut16(EEPROM_SIG_SLOT, MAGIC_SIG);

  }
  #ifdef MINMAX
  // reset max/min height
  minHeight = eepromGet16(MIN_HEIGHT_SLOT);
  maxHeight = eepromGet16(MAX_HEIGHT_SLOT);
  // check if 0 and fix/beep ...
  if (minHeight == 0) toggleMinHeight();
  if (maxHeight == 0) toggleMaxHeight();
  #endif
  bothbuttons = eepromGet16(BOTHBUTTON_SLOT);
#ifdef FEEDBACK
  feedback = eepromGet16(FEEDBACK_SLOT);
#endif

  // could also increment slot 1 every time to keep track of resets/power-cycles.
}


#ifdef MINMAX
// Swap the minHeight values and save in EEPROM
void toggleMinHeight()
{
  
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
    beep(minHeight, 1);
  }
  
  eepromPut16(MIN_HEIGHT_SLOT, minHeight);
}

// Swap the maxHeight values and save in EEPROM
void toggleMaxHeight()
{
  
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
    beep(maxHeight, 1);
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