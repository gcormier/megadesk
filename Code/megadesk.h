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
