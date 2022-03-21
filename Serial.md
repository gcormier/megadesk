# Serial commands
After the marker byte '<', there's the Command byte, then two fields of ascii integer digits. 
Terminate each field with any non-digit character eg !,./ or a line-break.
missing digits are interpreted as a 0 so <L.. is valid and passes two 0s to the L command.
First field (position) can be any number up to a maximum of 65535, the second field (slot) has a maximum of 255.

```
  data start (first byte)
    char   meaning
  -----------------
    <      start Rx Marker

  command (second byte)
    cmd    meaning
  -----------------
    +      increase
    -      decrease
    =      absolute
    C      Ask for current location
    S      Save location to (UpButton) EEPROM
    s      Save location to Downbutton EEPROM
    L      Load (Upbutton) location from EEPROM and move
    l      load Downbutton location from EEPROM and move
    W      Write arbitrary data to EEPROM
    R      Read arbitrary data from EEPROM
    T      play tone

  position (third/fourth bytes or 1st digit field. max 65535)
    cmd    meaning
  -----------------
    +-     relative to current position
    =SsW   absolute position
    T      tone frequency
    CRLl   (ignore)

  push_addr (fifth byte or 2nd digit field. max 255)
    cmd    meaning
  -----------------
    SLlWwR EEPROM pushCount/slot
    T      tone duration/4 ms. (250 == 1s)
    +-=C   (ignore)

```

Examples
```
<W3000,3.     # writes  location 3 with height 3000.
<T3000,255.   # plays a 3000Hz tone for 1sec
<C0.0.        # reports current height
<+300..       # nudge desk up by 300
<L,2.         # recall position from slot 2
<l,2.         # recall position from down-button slot 2
<R.0/         # read eeprom slot 0
```

# esphome configuration
esphome example configuration avalible at [esphome.md](esphome.md)
