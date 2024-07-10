SET avrdudeconf="C:\Users\Greg\AppData\Local\arduino15\packages\ATTinyCore\hardware\avr\1.2.3/avrdude.conf"

avrdude "-C%avrdudeconf%" -c usbtiny -p t841 -U lfuse:w:0xe2:m
avrdude "-C%avrdudeconf%" -c usbtiny -p t841 -U hfuse:w:0xd4:m
avrdude "-C%avrdudeconf%" -c usbtiny -p t841 -U efuse:w:0xf4:m

avrdude "-C%avrdudeconf%" -v -pattiny841 -cusbtiny -Uflash:w:megadesk.hex:i

