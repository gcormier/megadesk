/*
 Copyright 2011 G. Andrew Stone
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <inttypes.h>
#include "Arduino.h"
#include <HardwareSerial.h>

#define LIN_SERIAL            HardwareSerial
#define LIN_BREAK_DURATION    15    // Number of bits in the break.
#define LIN_TIMEOUT_IN_FRAMES 1     // Wait this many max frame times before declaring a read timeout.

class Lin
{
protected:
  LIN_SERIAL& serial;
  uint8_t txPin;               //  what pin # is used to transmit (needed to generate the BREAK signal)
  int     serialSpd;           //  in bits/sec. Also called baud rate
  uint16_t nominalFrameTime;
  uint16_t breakTime;
  uint8_t  delimitTime;

  // For Lin 1.X "start" should = 0, for Lin 2.X "start" should be the addr byte. 
  static uint8_t dataChecksum(const uint8_t* message, char nBytes,uint16_t start=0);
  static uint8_t addrParity(uint8_t addr);

  int read_withtimeout(int16_t &countDown);
  void serialBreak(void);

public:
  Lin(LIN_SERIAL& ser=Serial,uint8_t txPin=1);
  void begin(int speed);

  // Send a message right now, ignoring the schedule table.
  void send(uint8_t addr, const uint8_t* message, uint8_t nBytes);

  // Receive a message right now, returns 0xff if good checksum, # bytes received (including checksum) if checksum is bad.
  uint8_t recv(uint8_t addr, uint8_t* message, uint8_t nBytes);

};

