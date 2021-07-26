#!/bin/bash
# why doesn't this work? we can write flash no problem, we can read eeprom ok, but can't write it.
# do we need to erase first??
./avrdude.exe -p attiny841 -c usbtiny -B2  -U eeprom:w:eeprom_fixed.eep:i

