Megadesk can be controlled/queried using serial communications.

Connect serial to the following programming header pins:

* MISO -> RX
* SCK -> TX
* GND -> GND

Serial communications can use either human readable ascii or slightly more compact raw bytes.

# compilation flags
Megadesk needs to be recompiled and flashed first as the default image doesn't have the serial features.

Two \#define flags control megadesk serial communications:
1. SERIALCOMMS  - enables basic raw serial communications. required with every other flag below.
1. HUMANSERIAL  - enables human-readable ascii where each numeric field is separated by a non-digit character
1. SERIALECHO   - echo back the interpreted command before acting on it
1. SERIALERRORS - Report errors over serial - reports timeouts, bad motor reads, user errors...


# Human/Ascii Serial
After the marker byte '<', there's the Command byte, then two fields of ascii integer digits.

Terminate each field with any non-digit character eg !,./ or a line-break.

Missing digits are interpreted as zeros so <L.. is valid and passes two 0s to the L command.

First field (position) can be any number up to a maximum of 65535, the second field (slot) has a maximum of 255.

```
  data start (first byte)
    char   meaning
  -----------------
    <      start Rx Marker

  command (second byte)
    cmd    meaning
  -----------------
    +      increase position by
    -      decrease position by
    =      set absolute position
    C      Report current position
    S      Save position to (UpButton) EEPROM
    s      Save position to Downbutton EEPROM
    L      Load (Upbutton) position from EEPROM and move
    l      load Downbutton position from EEPROM and move
    W      Write arbitrary data to EEPROM
    R      Read arbitrary data from EEPROM
    T      play tone

  position (1st digit field or third/fourth bytes. max 65535)
    cmd    meaning
  -----------------
    +-     relative to current position
    =SsW   absolute position
    T      tone frequency
    CRLl   (ignore)

  push_addr (2nd digit field or fifth byte. max 255)
    cmd    meaning
  -----------------
    SLlWwR EEPROM pushCount/slot
    T      tone duration/4 ms. (250 == 1s)
    +-=C   (ignore)

```

# Raw serial
Without HUMANSERIAL, always transmit 5 raw bytes:

ascii '<', command ascii byte, raw-MSB-position, raw-LSB-position, raw-slot

The '<' is the sync byte and indicates the start of a new message.
Command bytes, position and slot ranges and values are the same as for Human serial.


# Examples
Human serial commands, when using HUMANSERIAL

line breaks can be used instead of the final '.'
```
<W3000,3.     # writes location 3 with position 3000.
<T3000,255.   # plays a 3000Hz tone for 1sec
<C0.0.        # reports current position
<+300..       # nudge desk up by 300
<=3000.       # move to absolute position 3000
<L,2.         # recall position from slot 2
<l,2.         # recall position from down-button slot 2
<R.0/         # read eeprom slot 0
<L,17.        # toggle feedback pips
<L,18.        # toggle 2 button mode
<L,15.        # reset megadesk
```

Megadesk Responses:
```
>W3000,3    # successful response writing to slot 3
>T3000,255  # successful tone-play.
>=1323,0    # response to <C.. current position
>=5805,0    # response to <+ <- <= current position
>L6914,2    # successful recall of slot 2, target position of 6914 + beep and move
>R5715,2    # reports content of slot 2.
```

Megadesk error Responses (if SERIALERRORS is \#defined):
```
>E0,1       # slot 1 is invalid slot for recall
>E0,33      # slot 33 is empty + sad trombone
>E6914,0    # error. requested position 6914 may be beyond min/max limits
Ec0,0       # invalid command 'c' (sent <c)
E<0,0       # too many txMarkers (sent <<)
DE2361,117  # legs reported positions differ by 117
8E2361,0    # communications problem with pid(leg) 8
!E57496,25  # 25ms delay was late by 7456us
```

# esphome configuration
esphome example configuration avalible at [esphome.md](esphome.md)
