#pragma once

byte empty[] = {0, 0, 0};

enum class State : byte
{
  OFF,
  STARTING,
  UP,
  DOWN,
  STOPPING1,
  STOPPING2,
  STOPPING3,
  STOPPING4,
  STARTING_RECAL,
  RECAL,
  END_RECAL
};

enum class Command : byte
{
  NONE,
  UP,
  DOWN,
};

enum class Button : byte
{
  NONE,
  UP,
  DOWN,
  BOTH,
};

// serial markers and operations
const char rxMarker = '<'; // start marker for comms coming to desk (Rx)
const char command_increase = '+';
const char command_decrease = '-';
const char command_absolute = '=';
const char command_save      = 'S';
const char command_save_down = 's';
const char command_load      = 'L';
const char command_load_down = 'l';
const char command_write     = 'W';
const char command_read      = 'R';
const char command_current   = 'C';
const char command_tone      = 'T';
// responses only..
const char txMarker = '>'; // default start marker from desk (Tx)
const char response_error    = 'E'; // indicates an error
const char response_calibration = 'X'; // indicates calibration is starting

void playTone(uint16_t freq, uint16_t duration);
void beep(uint16_t freq, byte count=1);

void delayUntil(unsigned long microSeconds);

void linInit();
void linBurst();

void sendInitPacket(byte a1 = 255, byte a2 = 255, byte a3 = 255, byte a4 = 255);
byte recvInitPacket(byte array[]);

void toggleMinHeight();
void toggleMaxHeight();
void toggleBothMode();
void toggleFeedback();

// serial functions
void writeSerial(byte operation, uint16_t position, uint8_t push_addr = 0, byte marker = txMarker);
void parseData(byte command, uint16_t position, uint8_t push_addr);

// eeprom
void initAndReadEEPROM(bool force);
uint16_t eepromGet16( int idx );
void eepromPut16( int idx, uint16_t val );

uint16_t loadMemory(uint8_t memorySlot);
void saveMemory(uint8_t memorySlot, uint16_t value);
