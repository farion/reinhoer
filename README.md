# reinhoer
Software for arduino based audio player hardware for children.

## Errors
Errors during runtime are communicated via serial and via beep codes.
Beep codes are listed here:

  * 1x beep => SD failed.
  * 2x beep => Folder can not be opened.
  * 3x beep => Folder not found.
  * 4x beep => No tracks in tracklist.
  * 5x beep => File not found. Can not play.

## Needed Libraries

  * SPI: https://www.arduino.cc/en/Reference/SPI
  * SD: https://www.arduino.cc/en/Reference/SD
  * VS1053: https://github.com/adafruit/Adafruit_VS1053_Library
  * RFID: https://github.com/miguelbalboa/rfid
  * Bounce2: https://github.com/thomasfredericks/Bounce2
  * MemoryFree: https://github.com/maniacbug/MemoryFree
