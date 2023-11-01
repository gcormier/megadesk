#include "esphome.h"

class Megadesk : public Component, public Sensor, public UARTDevice {
 public:
  Megadesk(UARTComponent *parent) : UARTDevice(parent) {}

  Sensor *raw_height = new Sensor();
  Sensor *min_height = new Sensor();
  Sensor *max_height = new Sensor();

  void setup() override {}

  int digits=0;

  int readdigits()
  {
    int r;
    while ((r = read()) > 0) {
      if ((r < 0x30) || (r > 0x39)) {
        // non-digit we're done, return what we have
        return digits;
      }
      // it's a digit, add with base10 shift
      digits = 10*digits + (r-0x30);
      // keep reading...
    }
    return -1;
  }

  void recvData()
  {
    const int numChars = 2;
    const int numFields = 4; // read/store all 4 fields for simplicity, use only the last 3.
    // static variables allows segmented/char-at-a-time decodes
    static uint16_t receivedBytes[numFields];
    static uint8_t ndx = 0;
    int r; // read char/digit

    // read 2 chars
    while ((ndx < numChars) && ((r = read()) != -1))
    {
      if ((ndx == 0) && (r != '>'))
      {
        // first char is not Tx, keep reading...
        continue;
      }
      receivedBytes[ndx] = r;
      ++ndx;
    }
    // read ascii digits
    while ((ndx >= numChars) && ((r = readdigits()) != -1)) {
      receivedBytes[ndx] = r;
      digits = 0; // clear
      if (++ndx == numFields) {
        // thats all 4 fields. parse/process them now and break-out.
        parseData(receivedBytes[1],
                  receivedBytes[2],
                  receivedBytes[3]);
        ndx = 0;
        return;
      }
    }
  }

  void parseData(uint8_t command, uint16_t position, uint8_t push_addr)
  {
    if (command == '=')
    {
      raw_height->publish_state(position);
    } else if (command == 'R'){
      if (push_addr == 11){
        min_height->publish_state(position);
      } else if (push_addr == 12){
        max_height->publish_state(position);
      }
    }
  }

  void loop() override {
    while (available()) {
      recvData();
    }
  }
};