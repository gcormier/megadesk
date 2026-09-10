#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace megadesk {

class Megadesk : public Component, public uart::UARTDevice {
 public:
  void set_raw_height_sensor(sensor::Sensor *s) { raw_height_ = s; }
  void set_min_height_sensor(sensor::Sensor *s) { min_height_ = s; }
  void set_max_height_sensor(sensor::Sensor *s) { max_height_ = s; }

  void setup() override {}

  void loop() override {
    while (available()) {
      int r = read();
      if (r < 0) {
        return;
      }

      process_byte_(static_cast<uint8_t>(r));
    }
  }

 protected:
  enum class ParseState : uint8_t {
    WAIT_FOR_MARKER,
    READ_COMMAND,
    READ_POSITION,
    READ_SLOT,
  };

  sensor::Sensor *raw_height_{nullptr};
  sensor::Sensor *min_height_{nullptr};
  sensor::Sensor *max_height_{nullptr};

  ParseState parse_state_{ParseState::WAIT_FOR_MARKER};
  uint8_t command_{0};
  uint32_t position_{0};
  uint32_t slot_{0};

  void start_frame_() {
    command_ = 0;
    position_ = 0;
    slot_ = 0;
    parse_state_ = ParseState::READ_COMMAND;
  }

  void reset_parser_() {
    command_ = 0;
    position_ = 0;
    slot_ = 0;
    parse_state_ = ParseState::WAIT_FOR_MARKER;
  }

  bool append_digit_(uint32_t &value, uint8_t digit, uint32_t maximum) {
    if (value > (maximum - digit) / 10U) {
      return false;
    }

    value = value * 10U + digit;
    return true;
  }

  void process_byte_(uint8_t c) {
    // '>' always starts a fresh Megadesk response.
    // This lets us recover immediately from a truncated/corrupt frame.
    if (c == '>') {
      start_frame_();
      return;
    }

    switch (parse_state_) {
      case ParseState::WAIT_FOR_MARKER:
        // Ignore echoes, diagnostics, noise, and anything else
        // until a Megadesk response marker arrives.
        return;

      case ParseState::READ_COMMAND:
        command_ = c;
        parse_state_ = ParseState::READ_POSITION;
        return;

      case ParseState::READ_POSITION:
        if (c >= '0' && c <= '9') {
          const uint8_t digit = c - '0';

          if (!append_digit_(position_, digit, 65535U)) {
            reset_parser_();
          }
          return;
        }

        // Megadesk writeSerial() separates position and slot with ','.
        if (c == ',') {
          parse_state_ = ParseState::READ_SLOT;
          return;
        }

        // Anything else means this response is malformed.
        reset_parser_();
        return;

      case ParseState::READ_SLOT:
        if (c >= '0' && c <= '9') {
          const uint8_t digit = c - '0';

          if (!append_digit_(slot_, digit, 255U)) {
            reset_parser_();
          }
          return;
        }

        // Megadesk terminates human-readable responses with '\n'.
        // Accept CR too so CRLF cannot hurt us.
        if (c == '\n' || c == '\r') {
          parseData(
              command_,
              static_cast<uint16_t>(position_),
              static_cast<uint8_t>(slot_));
        }

        reset_parser_();
        return;
    }
  }

  void parseData(uint8_t command, uint16_t position, uint8_t push_addr) {
    if (command == '=') {
      if (raw_height_) {
        raw_height_->publish_state(position);
      }
    } else if (command == 'R') {
      if (push_addr == 11 && min_height_) {
        min_height_->publish_state(position);
      } else if (push_addr == 12 && max_height_) {
        max_height_->publish_state(position);
      }
    }
  }
};

}  // namespace megadesk
}  // namespace esphome
