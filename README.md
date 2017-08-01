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
