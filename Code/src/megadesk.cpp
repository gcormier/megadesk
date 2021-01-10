#include <EEPROM.h>
#include "lin.h"
#include "megadesk.h"

#define HYSTERESIS            137
#define PIN_UP                10
#define PIN_DOWN              9
#define PIN_BEEP              7
#define PIN_SERIAL            1

#define BEEP_DURATION         125
#define BEEP_PAUSE            60
#define BEEP_FREQ_LOW         2600
#define BEEP_FREQ_HIGH        3000
#define BEEP_FREQ_ACK         2000

#define CLICK_TIMEOUT         400UL     // Timeout in MS.
#define CLICK_LONG            900UL     // Long/hold minimum time in MS.

#define FINE_MOVEMENT_VALUE   100       // Based on protocol decoding

// LIN commands/status
#define LIN_CMD_IDLE          252
#define LIN_CMD_RAISE         134
#define LIN_CMD_LOWER         133
#define LIN_CMD_FINE          135
#define LIN_CMD_FINISH        132
#define LIN_CMD_PREMOVE       196

#define LIN_MOTOR_BUSY        2
#define LIN_MOTOR_BUSY_FINE   3

// Changing these might be a really bad idea. They are sourced from
// decoding the OEM controller limits. If you really need a bit of extra travel
// you can fiddle with SAFETY, it's an extra buffer of a few units.
#define SAFETY                20
#define DANGER_MAX_HEIGHT     6777 - HYSTERESIS - SAFETY
#define DANGER_MIN_HEIGHT     162 + HYSTERESIS + SAFETY

// Related to multi-touch
bool button_pin_up, button_pin_down;
bool goUp, goDown;
bool previous, pushLong, waitingEvent = false;
int pushCount = 0;
unsigned long currentMillis;
unsigned long firstPush = 0, lastPush = 0, pushLength = 0;
//////////////////

Lin lin(Serial, PIN_SERIAL);

int currentHeight = -1;
int targetHeight = -1;
unsigned long end, d;
unsigned long t = 0;

// Set default to 96 but this might be change from EEPROM
unsigned int LIN_MOTOR_IDLE = 96;


bool memoryMoving = false;
Command user_cmd = Command::NONE;
State state = State::OFF;
State lastState = State::OFF;
uint16_t enc_target;

const int numChars = 10;
char receivedChars[numChars];
boolean newData = false;

const char command_increase = '+';
const char command_decrease = '-';
const char command_absolute = '=';
const char command_write = 'W';
const char command_load = 'L';
const char command_current = 'C';
const char command_read = 'R';

const char startMarker = '<';

void up(bool pushed) {
  if (pushed)
    user_cmd = Command::UP;
  else
    user_cmd = Command::NONE;
}

void down(bool pushed) {
  if (pushed)
    user_cmd = Command::DOWN;
  else
    user_cmd = Command::NONE;
}

void ack()
{
  previous = false;
  firstPush = lastPush = pushLength = 0;
  pushCount = 0;
  pushLong = false;
  waitingEvent = false;
  currentMillis = millis();
}

void setup() {
	pinMode(PIN_UP, INPUT_PULLUP);
	pinMode(PIN_DOWN, INPUT_PULLUP);
	pinMode(PIN_BEEP, OUTPUT);

  Serial1.begin(115200);

	delay(500);

	// Button Test Mode
	if (!digitalRead(PIN_UP))
	{
		while (true)
		{
			if (!digitalRead(PIN_DOWN))
				{
					beep(1, 2637);
					delay(150);
					beep(1, 2349);
					delay(150);
					beep(1, 2093);
					delay(500);
				}
			if (!digitalRead(PIN_UP))
			{
				beep(1, 2093);
				delay(150);
				beep(1, 2349);
				delay(150);
				beep(1, 2637);
				delay(500);

			}
			delay(50);
		}
	}

	if (!digitalRead(PIN_DOWN))
	{
		initAndReadEEPROM(true);
		while (true)
		{
			beep(1, 2093);
			delay(1000);
		}
	}

  beep(1, 2093);
  initAndReadEEPROM(false);
  beep(1, 2349);
  lin.begin(19200);
  beep(1, 2637);
  
  linInit();
  beep(1, 2794);
}

void readButtons()
{
  button_pin_down = !digitalRead(PIN_DOWN);
  goDown = button_pin_down;

  previous = button_pin_up;
  button_pin_up = !digitalRead(PIN_UP);
  
  currentMillis = millis();

  if (!previous && button_pin_up) // Just got pushed
  {
    if (firstPush == 0) // First time we have pushed
      firstPush = lastPush = currentMillis;
    else
      lastPush = currentMillis;
    // Otherwise, Nth time we have pushed, catch it on the release

  }
  else if (previous && button_pin_up) // Being held
  {
    pushLength = currentMillis;

    // Are we holding the first push (not a memory function)
    if ((pushLength - lastPush) > CLICK_TIMEOUT && pushCount == 0)
      goUp = true;
    else if ((pushLength - lastPush) > CLICK_LONG && pushCount > 0 && !pushLong)
    {
      beep(pushCount+1, BEEP_FREQ_ACK);
      pushLong = true;
    }
  }
  else if (previous && !button_pin_up && !goUp)  // Just got released and it's a memory call
  {
    pushCount++;
  }
  else if (previous && !button_pin_up && goUp)  // Just got released and we were moving
  {
    ack();
    goUp = false;
  }
  else    // State has not changed, and is not being held
  {
    if (firstPush == 0) // idle
      return;

    if (currentMillis - lastPush > CLICK_TIMEOUT) // Released
      waitingEvent = true;
  }

}


// modified from https://forum.arduino.cc/index.php?topic=396450.0
// remove endMarker and make use only header and next 5 bytes
//    Otherwise if a byte is 62 (>) then problems could occur
void recvWithStartMarker() {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    char rc;

    while (Serial1.available() > 0 && newData == false) {
        rc = Serial1.read();

        if (recvInProgress == true) {
            if (ndx >= 5) {
                receivedChars[ndx] = rc;
                ndx++;
                if (ndx >= numChars) {
                    ndx = numChars - 1;
                }
            }
            else {
                receivedChars[ndx] = '\0'; // terminate the string
                recvInProgress = false;
                ndx = 0;
                newData = true;
            }
        }

        else if (rc == startMarker) {
            recvInProgress = true;
        }
    }
}

void writeSerial(char operation, int position, int push_addr)
{
  Serial1.write(startMarker);
  Serial1.write(operation);
  Serial1.write(position);
  Serial1.write(position>>8);
  Serial1.write(push_addr);
  Serial1.write(push_addr>>8);

}

int BitShiftCombine( unsigned char x_high, unsigned char x_low)
{
  int combined;
  combined = x_high;
  combined = combined<<8;
  combined |= x_low;
  return combined;
}

void parseData()
{
  char command = receivedChars[0];
  int position = BitShiftCombine(receivedChars[1],receivedChars[2]);
  int push_addr = BitShiftCombine(receivedChars[3],receivedChars[4]);

  /*
  command (first bit)
    +    increase
    -    decrease
    =    absolute
    C    Ask for current location
    W    Write EEPROM
    R    Read EEPROM location
    L    Load EEPROM location

  position (second bit)
    +-   relitave to current
    =W   absolute
    CRL  (ignore)

  push_addr (third bit)
    WRL   EEPROM pushCount number
    *     (ignore)
  */

  if (command==command_increase){
    targetHeight = currentHeight + position;
    memoryMoving = true;
  }
  else if( command==command_decrease){
    targetHeight = currentHeight - position;
    memoryMoving = true;
  }
  else if(command==command_absolute){
    targetHeight = position;
    memoryMoving = true;
  }
  else if(command==command_write){
    if (position == 0){
      saveMemory(push_addr, currentHeight);
    }
    else {
      saveMemory(push_addr, position);
    }
  }
  else if(command==command_load){
    pushCount = push_addr;
    waitingEvent = true;
  }
  else if(command==command_current){
    writeSerial(command_absolute,currentHeight);
  }
  else if(command==command_read){
    writeSerial(command_read,loadMemory(push_addr),push_addr);
  }
}

void loop()
{
  linBurst();

  readButtons();

  recvWithStartMarker();
  
  if (newData == true) {
      parseData();
      newData = false;
  }

  // When we power on the first time, and have a height value read, set our target height to the same thing
  // So we don't randomly move on powerup.
  if (currentHeight > 5 && targetHeight == -1){
    targetHeight = currentHeight;
    writeSerial(command_absolute,targetHeight);
  }
  
  if (waitingEvent)
  {
		if (pushCount == 16)
		{
			toggleIdleParameter();
		}
		else if (pushCount > 0)
    {
      if (pushLong)
      {
        saveMemory(pushCount, currentHeight);
      }
      else
      {
        targetHeight = loadMemory(pushCount);
        writeSerial(command_load,targetHeight,pushCount);

				if (targetHeight == 0)
				{
					beep(1, 1865);
					beep(1, 1976);
					beep(1, 1865);
					targetHeight = currentHeight;
				}
				else
					memoryMoving = true;
      }
    }
      ack();
  }
  else if (goUp)
  {
    memoryMoving = false;
    targetHeight = currentHeight + HYSTERESIS + 1;
    writeSerial(command_increase,HYSTERESIS+1);

  }
  else if (goDown)
  {
    memoryMoving = false;
    targetHeight = currentHeight - HYSTERESIS - 1;
    writeSerial(command_decrease,HYSTERESIS-1);
  }
  else if (!memoryMoving)
    targetHeight = currentHeight;

  if (targetHeight > currentHeight && abs(targetHeight - currentHeight) > HYSTERESIS && currentHeight < DANGER_MAX_HEIGHT)
    up(true);
  else if (targetHeight < currentHeight && abs(targetHeight - currentHeight) > HYSTERESIS && currentHeight > DANGER_MIN_HEIGHT)
    down(true);
  else
  {
    up(false);
    targetHeight = currentHeight;
    memoryMoving = false;
  }

  // Override all logic above and disable if we aren't initialized yet.
  if (targetHeight < 5)
    up(false);


  // Wait before next cycle. 150ms on factory controller, 25ms seems fine.
  delay_until(25);
}

void linBurst()
{
  uint8_t node_a[4] = { 0, 0, 0, 0 };
  uint8_t node_b[4] = { 0, 0, 0, 0 };
  uint8_t cmd[3] = { 0, 0, 0 };

  // Send PID 17
  lin.send(17, empty, 3, 2);
  delay_until(5);

  // Recv from PID 09
  lin.recv(9, node_b, 3, 2);
  delay_until(5);

  // Recv from PID 08
  lin.recv(8, node_a, 3, 2);
  delay_until(5);

  
  // Send PID 16, 6 times
  for (uint8_t i = 0; i < 6; i++) {
    lin.send(16, 0, 0, 2);
    delay_until(5);
  }

  // Send PID 1
  lin.send(1, 0, 0, 2);
  delay_until(5);

  uint16_t enc_a = node_a[0] | (node_a[1] << 8);
  uint16_t enc_b = node_b[0] | (node_b[1] << 8);
  enc_target = enc_a;
  currentHeight = enc_a;

  // Send PID 18
  switch (state) {
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
      enc_target = getMin(enc_a, enc_b) + FINE_MOVEMENT_VALUE;
    else
      enc_target = getMin(enc_a, enc_b) - FINE_MOVEMENT_VALUE;
    
    cmd[2] = LIN_CMD_FINE;
    
    break;
  case State::STOPPING4:
    enc_target = getMax(enc_a, enc_b);
    cmd[2] = LIN_CMD_FINISH;
    break;
  }

  cmd[0] = enc_target & 0xFF;
  cmd[1] = enc_target >> 8;
  lin.send(18, cmd, 3, 2);

  switch (state) {
  case State::OFF:
    if (user_cmd != Command::NONE) {
      if (node_a[2] == LIN_MOTOR_IDLE && node_b[2] == LIN_MOTOR_IDLE) {
        state = State::STARTING;
      }
    }
    break;
  case State::STARTING:
    switch (user_cmd) {
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
    if (user_cmd != Command::UP || currentHeight >= DANGER_MAX_HEIGHT) {
      state = State::STOPPING1;
    }
    break;
  case State::DOWN:
    if (user_cmd != Command::DOWN || currentHeight <= DANGER_MIN_HEIGHT) {
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
    if (node_a[2] == LIN_MOTOR_IDLE && node_b[2] == LIN_MOTOR_IDLE) {
      state = State::OFF;
    }
    break;
  default:
    state = State::OFF;
    break;
  }
}

void saveMemory(int memorySlot, int value)
{
  // Sanity check
  if (memorySlot == 0 || memorySlot == 1 || value < 5 || value > 32700)
    return;

	//beep(memorySlot, BEEP_FREQ_HIGH);

	EEPROM.put(2 * memorySlot, value);
}

int loadMemory(int memorySlot)
{
  if (memorySlot == 0 || memorySlot == 1)
    return currentHeight;

  beep(memorySlot, BEEP_FREQ_LOW);
	
	int memHeight;

	EEPROM.get(2 * memorySlot, memHeight);
  
	if (memHeight == 0)
	{
		beep(1, 1865);
		beep(1, 1976);
		beep(1, 1865);
	}

	return memHeight;
}

void delay_until(unsigned long microSeconds) {
  end = t + (1000 * microSeconds);
  d = end - micros();

  // crazy long delay; probably negative wrap-around
  // just return
  if (d > 1000000) {
    t = micros();
    return;
  }

  if (d > 15000) {
    unsigned long d2 = (d - 15000) / 1000;
    delay(d2);
    d = end - micros();
  }
  delayMicroseconds(d);
  t = end;
}

void beep(int count, int freq)
{
  for (int i = 0; i < count; i++)
  {
    tone(PIN_BEEP, freq);
    delay(BEEP_DURATION);
    noTone(PIN_BEEP);
    delay(BEEP_PAUSE);
  }
}

void sendInitPacket(uint8_t a1, uint8_t a2, uint8_t a3, uint8_t a4)
{
  uint8_t packet[8] = { a1, a2, a3, a4, 255, 255, 255, 255 };
  
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
  uint8_t resp[8];
  
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
  
  byte initA = 0;
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

  byte initB = initA+1;
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

  byte initC = initB + 1;
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

  uint8_t magicPacket[3] = { 246, 255, 191 };
  lin.send(18, magicPacket, 3, 2);
  
  delay(5);
  

}


uint8_t recvInitPacket(uint8_t array[])
{
  return lin.recv(61, array, 8, 2);
}

// Return the smaller of the two parameters
uint16_t getMin(uint16_t a, uint16_t b)
{
  if (a < b)
    return a;
  else
    return b;
}

// Return the bigger of the two parameters
uint16_t getMax(uint16_t a, uint16_t b)
{
  if (a > b)
    return a;
  else
    return b;
}

void initAndReadEEPROM(bool force)
{
	int a = EEPROM.read(0);
	int b = EEPROM.read(1);

	if ((a != 18 && b != 13) || force)
	{
		for (unsigned int index = 0; index < EEPROM.length(); index++)
			EEPROM.write(index, 0);

		// Store unique values
		EEPROM.write(0, 18);
		EEPROM.write(1, 13);

		// This is the idle value
		EEPROM.write(2, 96);
	}


	// Read this value
	LIN_MOTOR_IDLE = EEPROM.read(2);

}


// Swap the IDLE values and save in EEPROM
void toggleIdleParameter()
{
	LIN_MOTOR_IDLE = EEPROM.read(2);

	if (LIN_MOTOR_IDLE == 96)
	{
		LIN_MOTOR_IDLE = 0;
		for (int i = 0; i < 3; i++)
		{
			beep(1, 2637);
			delay(10);
			beep(1, 2349);
			delay(10);
			beep(1, 2093);
			delay(10);
		}
	}
	
	else
	{
		for (int i = 0; i < 3; i++)
		{
			beep(1, 2093);
			delay(50);
			beep(1, 2349);
			delay(50);
			beep(1, 2637);
			delay(50);
		}
		LIN_MOTOR_IDLE = 96;
	}

	EEPROM.write(2, LIN_MOTOR_IDLE);
}