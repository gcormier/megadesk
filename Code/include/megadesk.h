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
};

enum class Command : byte
{
  NONE,
  UP,
  DOWN,
};

void beep(byte count, int freq);
void initAndReadEEPROM(bool force);
void linInit();
void linBurst();

void recvWithStartEndMarkers();
void writeSerial(char operation, int position, uint8_t push_addr = 0);
int BitShiftCombine(uint8_t x_high, uint8_t x_low);
void parseData();

void delay_until(unsigned long microSeconds);

void sendInitPacket(byte a1 = 255, byte a2 = 255, byte a3 = 255, byte a4 = 255);
byte recvInitPacket(byte array[]);

uint16_t getMax(uint16_t a, uint16_t b);
uint16_t getMin(uint16_t a, uint16_t b);

#ifdef MINMAX
void toggleMinHeight();
void toggleMaxHeight();
#endif

int loadMemory(uint8_t memorySlot);
void saveMemory(uint8_t memorySlot, int value);
