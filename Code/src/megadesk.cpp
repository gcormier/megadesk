// Uncomment this define if you want serial
//#define SERIALCOMMS

// Uncomment this if you want to override minimum/maximum heights
//#define MINMAX

// Uncomment to store/recall memories from both buttons
#define BOTHBUTTONS

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
#define SHORT_PAUSE 50
#define PAUSE BEEP_DURATION
#define LONG_PAUSE 500
#define ONE_SEC_PAUSE 1000
#define PITCH_ADJUST (48000000 / F_CPU) // digitalWrite takes ~6us at 8MHz

// notes to beep
#define NOTE_G6 1568
#define NOTE_B6 1976
#define NOTE_C7 2093
#define NOTE_CSHARP7 2217
#define NOTE_D7 2344
#define NOTE_DSHARP7 2489
#define NOTE_E7 2637
#define NOTE_F7 2794
#define NOTE_G7 3136
#define NOTE_C8 4186
#define NOTE_LOW NOTE_C7
#define NOTE_ACK NOTE_G7
#define NOTE_HIGH NOTE_C8

#define CLICK_TIMEOUT 400UL // Timeout in MS.
#define CLICK_LONG 900UL    // Long/hold minimum time in MS.

#define FINE_MOVEMENT_VALUE 100 // Based on protocol decoding

// LIN commands/status
#define LIN_CMD_IDLE 252
#define LIN_CMD_RAISE 134
#define LIN_CMD_LOWER 133
#define LIN_CMD_FINE 135
#define LIN_CMD_FINISH 132
#define LIN_CMD_PREMOVE 196

#define LIN_MOTOR_BUSY 2
#define LIN_MOTOR_BUSY_FINE 3

// These are the 3 known idle states
#define LIN_MOTOR_IDLE1 0
#define LIN_MOTOR_IDLE2 37
#define LIN_MOTOR_IDLE3 96

// Changing these might be a really bad idea. They are sourced from
// decoding the OEM controller limits. If you really need a bit of extra travel
// you can fiddle with SAFETY, it's an extra buffer of a few units.
#define SAFETY 20
#define DANGER_MAX_HEIGHT 6777 - HYSTERESIS - SAFETY
#define DANGER_MIN_HEIGHT 162 + HYSTERESIS + SAFETY

// constants related to presses/eeprom slots
// (on attiny841: 512byte eeprom means max 255 slots)
#define MIN_HEIGHT_SLOT 20
#define MAX_HEIGHT_SLOT 22
#define RIGHT_SLOT_START 32 // 0x20 in hex

// EEPROM magic signature to detect if eeprom is valid
#define EEPROM_LOCATION_SIG 0
#define MAGIC_SIG 0x120d // bytes: 13, 18 in little endian order

// is a memory button?
#ifdef BOTHBUTTONS
#define MEMORY_BUTTON(button) ((button == Button::UP) || (button == Button::DOWN))
#else
#define MEMORY_BUTTON(button) (button == Button::UP)
#endif
#define PRESSED(button) (button != Button::NONE)

// Tracking multipress/longpress of buttons
Button previous = Button::NONE; // previous state of buttons
Button lastbutton = Button::NONE; // last button to be pressed
uint8_t pushCount = 0; // tracks button pushes
bool savePosition = false; // flag to save currentHeight to pushCount slot
bool memoryEvent = false; // flag to load/save a pushCount slot
bool manualUp, manualDown; // manual-mode. when holding first press of up/down

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
uint16_t oldHeight = 0;

const int numBytes = 4;
byte receivedBytes[numBytes];
byte newData = false;

const char command_increase = '+';
const char command_decrease = '-';
const char command_absolute = '=';
const char command_write = 'W';
const char command_load = 'L';
const char command_current = 'C';
const char command_read = 'R';

const char startMarker = '<'; 
#endif

// set/clear motor up command
void motorUp(bool go)
{
  if (go)
    user_cmd = Command::UP;
  else
    user_cmd = Command::NONE;
}

// set/clear motor down command
void motorDown(bool go)
{
  if (go)
    user_cmd = Command::DOWN;
  else
    user_cmd = Command::NONE;
}

// clean slate for button presses
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
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BEEP, OUTPUT);

  delay(LONG_PAUSE);

  // Button Test Mode
  if (!digitalRead(PIN_UP))
  {
    while (true)
    {
      if (!digitalRead(PIN_DOWN))
      {
        beep(NOTE_E7);
        delay(PAUSE);
        beep(NOTE_D7);
        delay(PAUSE);
        beep(NOTE_C7);
        delay(LONG_PAUSE);
      }
      if (!digitalRead(PIN_UP))
      {
        beep(NOTE_C7);
        delay(PAUSE);
        beep(NOTE_D7);
        delay(PAUSE);
        beep(NOTE_E7);
        delay(LONG_PAUSE);
      }
      delay(SHORT_PAUSE);
    }
  }

  // factory reset
  if (!digitalRead(PIN_DOWN))
  {
    initAndReadEEPROM(true);
    while (true)
    {
      beep(NOTE_C7);
      delay(ONE_SEC_PAUSE);
    }
  }

#ifdef SERIALCOMMS
  Serial1.begin(19200);
#endif

  // init + arpeggio
  beep(NOTE_C7);
  initAndReadEEPROM(false);
  beep(NOTE_E7);
  lin.begin(19200);
  beep(NOTE_G7);

  linInit();
  beep(NOTE_C8);
}

// track button presses - short and long
void readButtons()
{
  Button buttons;
  bool stop = false;

  unsigned long currentMillis = millis();

  if (!digitalRead(PIN_UP)) {
    if (!digitalRead(PIN_DOWN)) {
      // invalid state
      buttons = Button::BOTH;
      stop = true;
    } else {
      buttons = Button::UP;
    }
  } else if (!digitalRead(PIN_DOWN)) {
    buttons = Button::DOWN;
#ifndef BOTHBUTTONS
    manualDown = true;
#endif
  } else {
    buttons = Button::NONE;
#ifndef BOTHBUTTONS
    manualDown = false;
#endif
  }

  // If already moving and any button is pressed - stop
  if ( memoryMoving && PRESSED(buttons))
    targetHeight = currentHeight;

  if ( !PRESSED(previous) && MEMORY_BUTTON(buttons) ) // Just pushed
  {
#ifdef BOTHBUTTONS
    // clear pushCount if pushing a different button from last
    if (buttons != lastbutton) startFresh();
#endif

    lastPushTime = currentMillis;
    lastbutton = buttons;
  }
  else if ((previous == buttons) && MEMORY_BUTTON(buttons) ) // button held
  {
    unsigned long pushLength = currentMillis - lastPushTime;

    // long push?
    if (pushLength > CLICK_TIMEOUT) {
      // first long push? then move!
      if (pushCount == 0) {
        if (buttons == Button::UP) manualUp = true;
        if (buttons == Button::DOWN) manualDown = true;
      }
      else if (!savePosition)
      {
        // longpress after previous pushes - save this position
        beep(NOTE_ACK, pushCount + 1); // first provide feedback to release...
        savePosition = true;
      }
    }
  }
  else if ( MEMORY_BUTTON(previous) && !PRESSED(buttons) ) // just released
  {
    // moving?
    if ( !manualUp && !manualDown )
    {
      // not moving manually
      pushCount++; // short press increase the count
    }
    if ( manualUp || manualDown )
    {
      // we were under manual control before, stop now
      stop = true;
    }
  }
  else // other states: both-buttons / idle etc.
  {
    // check timeout on release - indicating last press
    if ((lastPushTime != 0) && (currentMillis - lastPushTime > CLICK_TIMEOUT)) // Released
    {
      // last press
      if (pushCount > 1)
        memoryEvent = true; // either store or recall a setting
      else
        stop = true; // or start fresh
    }
  }

  if (stop) {
    startFresh();
    manualUp = false;
    manualDown = false;
  }
  previous = buttons;
}

// modified from https://forum.arduino.cc/index.php?topic=396450.0
#ifdef SERIALCOMMS
void recvData()
{

  uint8_t recvInProgress = false;
  uint8_t ndx = 0;

  while (Serial1.available() > 0 && newData == false)
  {
    char rc = Serial1.read();

    if (recvInProgress == true)
    {
      if (ndx < numBytes - 1)
      {
        receivedBytes[ndx] = rc;
        ndx++;
      }
      else
      {
        receivedBytes[ndx] = rc;
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }
    else if (rc == startMarker)
    {
      recvInProgress = true;
    }
  }
}

void writeSerial(char command, uint16_t position, uint8_t push_addr)
{
  byte tmp[2];
  Serial1.write(startMarker);
  Serial1.write(command);
  tmp[1] = (position >> 8);
  tmp[0] = position & 0xff;
  Serial1.write(tmp[1]);
  Serial1.write(tmp[0]);
  Serial1.write(push_addr);
}

void parseData()
{
  char command = receivedBytes[0];
  int position = makeWord(receivedBytes[1], receivedBytes[2]);
  uint8_t push_addr = receivedBytes[3];

  /*
  data start (first byte)
    <    start data sentence

  command (second byte)
    +    increase
    -    decrease
    =    absolute
    C    Ask for current location
    W    Write EEPROM
    R    Read EEPROM location
    L    Load EEPROM location

  position (third/fourth bytes)
    +-   relitave to current
    =W   absolute
    CRL  (ignore)

  push_addr (fifth byte)
    WRL   EEPROM pushCount number
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
    pushCount = push_addr;
    memoryEvent = true;
  }
  else if (command == command_read)
  {
    writeSerial(command_read, eepromGet16(push_addr), push_addr);
  }
}
#endif

void loop()
{
  linBurst();

#ifdef SERIALCOMMS
  if (memoryMoving == false && oldHeight != currentHeight){
    if (oldHeight < currentHeight){
      writeSerial(command_increase, currentHeight-oldHeight, pushCount);
    }
    else {
      writeSerial(command_decrease, oldHeight-currentHeight, pushCount);
    }
  }
#endif

  readButtons();

#ifdef SERIALCOMMS
  recvData();

  if (newData == true)
  {
    parseData();
    newData = false;
  }
#endif

  // When we power on the first time, and have a height value read, set our target height to the same thing
  // So we don't randomly move on powerup.
  if (currentHeight > 5 && targetHeight == 0)
  {
    targetHeight = currentHeight;
  }

  if (memoryEvent)
  {
#ifdef MINMAX
    if (pushCount == MIN_HEIGHT_SLOT)
    {
      toggleMinHeight();
    }
    else if (pushCount == MAX_HEIGHT_SLOT)
    {
      toggleMaxHeight();
    } else // else if continued next line
#endif
    if (savePosition)
    {
#ifdef BOTHBUTTONS
      if (lastbutton == Button::DOWN) pushCount += RIGHT_SLOT_START;
#endif
      saveMemory(pushCount, currentHeight);
#ifdef SERIALCOMMS
      writeSerial(command_write, currentHeight, pushCount);
#endif
    }
    else
    {
      // load position
      beep(NOTE_LOW, pushCount);
#ifdef BOTHBUTTONS
      if (lastbutton == Button::DOWN) pushCount += RIGHT_SLOT_START;
#endif
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
  else if (manualUp)
  {
    memoryMoving = false;
    targetHeight = currentHeight + HYSTERESIS + 1;
  }
  else if (manualDown)
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
    motorUp(true);
  else if (targetHeight < currentHeight && abs(targetHeight - currentHeight) > HYSTERESIS && currentHeight > minHeight)
    motorDown(true);
  else
  {
    motorUp(false);
    targetHeight = currentHeight;
    memoryMoving = false;
  }

  // Override all logic above and disable if we aren't initialized yet.
  if (targetHeight < 5)
    motorUp(false);

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
  State lastState = State::OFF;

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
    beep(NOTE_DSHARP7);
    beep(NOTE_D7);
    beep(NOTE_CSHARP7);
    beep(NOTE_C7);
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
// freq is in Hz. duration is in ms. (max 524ms)
void playTone(uint16_t freq, uint16_t duration) {
  uint16_t halfperiod = 1000000L / freq; // in us.
  // mostly equivalent to:
  // for (long i = 0; i < duration * 1000L; i += halfperiod * 2) {
  for ( duration = (125 * duration) / (halfperiod / 4); duration > 0; duration -= 1 ) {
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
  int signature = eepromGet16(EEPROM_LOCATION_SIG);

  if ((signature != MAGIC_SIG) || force)
  {
    for (unsigned int index = 0; index < EEPROM.length(); index++)
      EEPROM.write(index, 0);
    // Store signature value
    eepromPut16(EEPROM_LOCATION_SIG, MAGIC_SIG);

    #ifdef MINMAX
    // reset max/min height
    eepromPut16(MIN_HEIGHT_SLOT, DANGER_MIN_HEIGHT);
    eepromPut16(MAX_HEIGHT_SLOT, DANGER_MAX_HEIGHT);
    #endif
  }
  #ifdef MINMAX
  minHeight = eepromGet16(MIN_HEIGHT_SLOT);
  maxHeight = eepromGet16(MAX_HEIGHT_SLOT);
  #endif
}


#ifdef MINMAX
// Swap the minHeight values and save in EEPROM
void toggleMinHeight()
{
  
  if (minHeight == DANGER_MIN_HEIGHT)
  {
    minHeight = currentHeight;
  }
  else
  {
    minHeight = DANGER_MIN_HEIGHT;
  }
  
  //Min height change sound
  beep(minHeight, 4); // tone based upon new minHeight

  eepromPut16(MIN_HEIGHT_SLOT, minHeight);
}

// Swap the maxHeight values and save in EEPROM
void toggleMaxHeight()
{
  
  if (maxHeight == DANGER_MAX_HEIGHT)
  {
    maxHeight = currentHeight;
  }
  else
  {
    maxHeight = DANGER_MAX_HEIGHT;
  }

  //Max height change sound
  beep(maxHeight, 4); // tone based upon new maxHeight

  eepromPut16(MAX_HEIGHT_SLOT, maxHeight);
}
#endif
