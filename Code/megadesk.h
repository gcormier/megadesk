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

void initEEPROM()
{
  int a = EEPROM.read(0);
  int b = EEPROM.read(1);
  int c = EEPROM.read(2);

  if (a != 18 && b != 13 && c != 19)
  {
    for (int index = 0; index < EEPROM.length(); index++)
      EEPROM.write(index, 100);

    // Store unique values
    EEPROM.write(0, 18);
    EEPROM.write(1, 13);
    EEPROM.write(2, 19);

  }
}


