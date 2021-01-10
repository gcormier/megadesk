#pragma once

uint8_t empty[] = { 0, 0, 0 };


enum class State {
  OFF,
  STARTING,
  UP,
  DOWN,
  STOPPING1,
  STOPPING2,
  STOPPING3,
  STOPPING4,
};

enum class Command {
  NONE,
  UP,
  DOWN,
};

void beep(int count, int freq);
void initAndReadEEPROM(bool force);
void linInit();
void linBurst();

void recvWithStartEndMarkers();
void writeSerial(char operation, int position, int push_addr = -1); //fix default?
int BitShiftCombine( unsigned char x_high, unsigned char x_low);
void parseData();

void delay_until(unsigned long microSeconds);

void sendInitPacket(uint8_t a1 = 255, uint8_t a2 = 255, uint8_t a3 = 255, uint8_t a4 = 255);
uint8_t recvInitPacket(uint8_t array[]);

uint16_t getMax(uint16_t a, uint16_t b);
uint16_t getMin(uint16_t a, uint16_t b);
void toggleIdleParameter();

int loadMemory(int memorySlot);
void saveMemory(int memorySlot, int value);
