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

void playTone(uint16_t freq, uint16_t duration);
void beep(uint16_t freq, byte count=1);
void initAndReadEEPROM(bool force);
void linInit();
void linBurst();

void recvWithStartEndMarkers();
void writeSerial(char operation, uint16_t position, uint8_t push_addr = 0);
int BitShiftCombine(uint8_t x_high, uint8_t x_low);
void parseData();

void delayUntil(unsigned long microSeconds);

void sendInitPacket(byte a1 = 255, byte a2 = 255, byte a3 = 255, byte a4 = 255);
byte recvInitPacket(byte array[]);

#ifdef MINMAX
void toggleMinHeight();
void toggleMaxHeight();
#endif
void toggleBothMode();
void toggleFeedback();

uint16_t eepromGet16( int idx );
void eepromPut16( int idx, uint16_t val );

uint16_t loadMemory(uint8_t memorySlot);
void saveMemory(uint8_t memorySlot, uint16_t value);
