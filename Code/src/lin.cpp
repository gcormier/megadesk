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

#include "Arduino.h"
#include "lin.h"

Lin::Lin(LIN_SERIAL& ser,uint8_t TxPin)
  : serial(ser)
  , txPin(TxPin)
{
}

// define bit period without () so that integer calculations can be more accurate
#define TBIT 1000000UL/serialSpd
void Lin::begin(int speed)
{
  serialSpd = speed;
  serial.begin(serialSpd);

  // header 34 bits = 13 break, 1 delimin, 10 sync, 10 addr
  nominalFrameTime = (34+90)*TBIT;  // 10bits/byte * 9 (payload bytes + checksum)
  breakTime = LIN_BREAK_DURATION*TBIT; // 780us break time
  delimitTime = TBIT;  // 52us for bit period

  pinMode(txPin, OUTPUT);
}


// Generate a BREAK signal (a low signal for longer than a byte) across the serial line
void Lin::serialBreak(void)
{
  serial.end();

  digitalWrite(txPin, LOW);  // Send BREAK
  delayMicroseconds(breakTime);

  digitalWrite(txPin, HIGH);  // BREAK delimiter
  delayMicroseconds(delimitTime);

  serial.begin(serialSpd);
}

/* Lin defines its checksum as an inverted 8 bit sum with carry */
uint8_t Lin::dataChecksum(const uint8_t* message, char nBytes,uint16_t sum)
{
    while (nBytes-- > 0) sum += *(message++);
    // Add the carry
    while(sum>>8)  // In case adding the carry causes another carry
      sum = (sum&255)+(sum>>8); 
    return (~sum);
}

/* Create the Lin ID parity */
#define BIT(data,shift) ((addr>>shift)&1)
uint8_t Lin::addrParity(uint8_t addr)
{
  uint8_t p0 = BIT(addr,0) ^ BIT(addr,1) ^ BIT(addr,2) ^ BIT(addr,4);
  uint8_t p1 = ~(BIT(addr,1) ^ BIT(addr,3) ^ BIT(addr,4) ^ BIT(addr,5));
  return (p0 | (p1<<1))<<6;
}

void Lin::send(uint8_t addr, const uint8_t* message, uint8_t nBytes)
{
  uint8_t addrbyte = (addr & 0x3f) | addrParity(addr);
  // LIN diagnostic (60) frame shall always use CHKSUM of protocol version 1.x.
  uint8_t cksum = dataChecksum(message,nBytes,(addr == 0x3c) ? 0: addrbyte);
  serialBreak();       // Generate the low signal that exceeds 1 char.
  serial.write(0x55);  // Sync byte
  serial.write(addrbyte);  // ID byte
  serial.write(message, nBytes);  // data bytes
  serial.write(cksum);  // checksum
  serial.flush();
}

// read serial or wait until countDown hits zero.
int Lin::read_withtimeout(int16_t &countDown)
{
  while(!serial.available())
  {
    delayMicroseconds(100);
    countDown-= 100;
    if (countDown<=0) return -1;
  }
  return serial.read();
}

// returns character read or:
// returns 0xfd if serial isn't echoing
// returns 0xfe if serial echoed only the sync - a fluke?
// returns 0xff for checksum error
// returns 0 if no characters.
uint8_t Lin::recv(uint8_t addr, uint8_t* message, uint8_t nBytes)
{
  uint8_t bytesRcvd=0;
  int16_t readByte;
  uint8_t idByte = (addr&0x3f) | addrParity(addr);
  int16_t countDown = nominalFrameTime * LIN_TIMEOUT_IN_FRAMES;
  serialBreak();       // Generate the low signal that exceeds 1 char.
  serial.write(0x55);  // Sync byte
  serial.write(idByte);  // ID byte
  serial.flush();
  bytesRcvd = 0xfd;
  do { // I hear myself
    readByte = read_withtimeout(countDown);
  } while(readByte != 0x55 && readByte != -1);
  bytesRcvd = 0xfe;
  do {
    readByte = read_withtimeout(countDown);
  } while(readByte != idByte && readByte != -1);
  bytesRcvd = 0;
  // This while loop strategy does not take into account the added time for the logic.  So the actual timeout will be slightly longer then written here.
  for (uint8_t i=0;i<nBytes;i++)
  {
    readByte = read_withtimeout(countDown);
    message[i] = readByte;
    if (readByte == -1) goto done;
    bytesRcvd++;
  }
  readByte = read_withtimeout(countDown);
  bytesRcvd++;
  if (dataChecksum(message,nBytes,(addr == 0x3d) ? 0: idByte) != readByte) bytesRcvd = 0xff;

done:
  serial.flush();

  return bytesRcvd;
}


