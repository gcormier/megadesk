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

void Lin::begin(int speed)
{
  serialSpd = speed;
  serial.begin(serialSpd);

  pinMode(txPin, OUTPUT);
}


// Generate a BREAK signal (a low signal for longer than a byte) across the serial line
void Lin::serialBreak(void)
{
  serial.flush();
  serial.end();

  uint16_t brkend = (1000000UL/serialSpd); // comes to 52us for symbol period
  uint16_t brkbegin = brkend*LIN_BREAK_DURATION; // 780us break time
  
  digitalWrite(txPin, LOW);  // Send BREAK
  if (brkbegin > 16383) delay(brkbegin/1000);  // delayMicroseconds unreliable above 16383 see arduino man pages
  else delayMicroseconds(brkbegin);
  
  digitalWrite(txPin, HIGH);  // BREAK delimiter

  if (brkend > 16383) delay(brkend/1000);  // delayMicroseconds unreliable above 16383 see arduino man pages
  else delayMicroseconds(brkend);

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

void Lin::send(uint8_t addr, const uint8_t* message, uint8_t nBytes, int16_t cksum)
{
  uint8_t addrbyte = (addr & 0x3f) | addrParity(addr);
  if (cksum == -1)
    cksum = dataChecksum(message,nBytes,addrbyte);
  serialBreak();       // Generate the low signal that exceeds 1 char.
  serial.write(0x55);  // Sync byte
  serial.write(addrbyte);  // ID byte
  serial.write(message, nBytes);  // data bytes
  serial.write(cksum);  // checksum
  serial.flush();
}

// read serial or wait until timeoutCount hits zero.
int Lin::read_withtimeout(int16_t &timeoutCount)
{
  while(!serial.available())
  {
    delayMicroseconds(100);
    timeoutCount-= 100;
    if (timeoutCount<=0) return -1;
  }
  return serial.read();
}

// returns character read or:
// returns 0xfd if serial isn't echoing
// returns 0xfe if serial echoed only one char - a fluke?
// returns 0 if no characters or if checksum failed.
uint8_t Lin::recv(uint8_t addr, uint8_t* message, uint8_t nBytes)
{
  uint8_t bytesRcvd=0;
  int16_t readByte;
  uint8_t idByte = (addr&0x3f) | addrParity(addr);
  uint16_t Tbit_times_ten = 1000000/serialSpd;  // symbol period is Tbit*10
  uint16_t nominalFrameTime_times_ten = (34+90)*Tbit_times_ten;  // 34 header bit length, 9 = # payload bytes + checksum.
  int16_t timeoutCount = LIN_TIMEOUT_IN_FRAMES * nominalFrameTime_times_ten;  // comes to 12916us
  serialBreak();       // Generate the low signal that exceeds 1 char.
  serial.write(0x55);  // Sync byte
  serial.write(idByte);  // ID byte
  serial.flush();
  bytesRcvd = 0xfd;
  do { // I hear myself
    readByte = read_withtimeout(timeoutCount);
  } while(readByte != 0x55 && readByte != -1);
  bytesRcvd = 0xfe;
  do {
    readByte = read_withtimeout(timeoutCount);
  } while(readByte != idByte && readByte != -1);
  bytesRcvd = 0;
  // This while loop strategy does not take into account the added time for the logic.  So the actual timeout will be slightly longer then written here.
  for (uint8_t i=0;i<nBytes;i++)
  {
    readByte = read_withtimeout(timeoutCount);
    message[i] = readByte;
    if (readByte == -1) goto done;
    bytesRcvd++;
  }
  readByte = read_withtimeout(timeoutCount);
  bytesRcvd++;
  if (dataChecksum(message,nBytes,idByte) == readByte) bytesRcvd = 0xff;

done:
  serial.flush();

  return bytesRcvd;
}


