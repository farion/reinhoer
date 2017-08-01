#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <MFRC522.h> // RFID Bibliothek Laden
#include <Bounce2.h>
#include <LinkedList.h>
#include "src/MemoryFree/MemoryFree.h"

/**
   ERRORS

   1x beep => SD failed.
   2x beep => Folder can not be opened.
   3x beep => Folder not found.
   4x beep => No tracks in tracklist.
   5x beep => File not found. Can not play.
*/

#define COMMON_ANODE // Uncomment if your LED has a common anode

#define VS_RESET 13 // VS1053 reset pin (output)
#define VS_CS 10 // VS1053 chip select pin (output)
#define VS_DCS 8 // VS1053 Data/command select pin (output)
#define VS_DREQ 3 // VS1053 Data request pin (into Arduino)
#define SD_CS 6 // Card chip select pin

#define RFC_CS 2 // CS for RFC
#define RFC_RESET 11 //RESET for RFC

#define BTN_NEXT 4 // Arduino pin for next button
#define BTN_PREV 13 // Arduino pin for prev button
#define BTN_VOL_UP 12 // Arduino pin for vol up button
#define BTN_VOL_DOWN 5 // Arduino pin for vol down button

#define RED_LED A2 // Arduino pin for led red channel
#define GREEN_LED A1 // Arduino pin for led green channel
#define BLUE_LED A0 // Arduino pin for led blue channel

#define VOL_INIT 40 // Initial volume (0-100)
#define MAX_VOL 100 // Maximal volume (0-100)

#define RFC_MIN_INTERVAL 1000 // Interval in milliseconds between two RFID changes. Leads to less flickering between multiple RFID tokens.

MFRC522 mfrc522(RFC_CS, RFC_RESET);

Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(VS_RESET, VS_CS, VS_DCS, VS_DREQ, SD_CS);

long currentCode = 0;
int currentTrack = 0;
LinkedList<String> trackList = LinkedList<String>(); //TODO solve with char* to reduce memory usage
long cardDeadCounter = 0;
int currentVolume = VOL_INIT;
unsigned long lastRfcChange = 0;

Bounce debouncerNext = Bounce();
Bounce debouncerPrev = Bounce();
Bounce debouncerVolUp = Bounce();
Bounce debouncerVolDown = Bounce();

void goError(String message, int beepCount) {
  setColor(186, 0, 0); // Set LED to red
  for (int i = 0; i < beepCount; i++) {
    musicPlayer.sineTest(0x44, 300);
  }
  Serial.print("[ERR] ");
  Serial.println(message);
}

void setup() {
  Serial.begin(9600);

  //initialize LED
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  //set LED to white
  setColor(186, 186, 186);
  
  SPI.begin();

  mfrc522.PCD_Init();

  // initialise the music player
  if (!musicPlayer.begin()) {
    setColor(186, 0, 0); // Set LED to red
    Serial.println("[ERR] VS1053 not found");
    exit(1);
  } else {
    Serial.println("[INF] VS1053 found");
  }

  int volume_intial = min(VOL_INIT, MAX_VOL);

  musicPlayer.setVolume(volume_intial,volume_intial); // Set initial volume
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); // DREQ int

  //initialize SD card
  if (!SD.begin(SD_CS)) {
    goError("SD failed", 1);
    exit(1);
  } else {
    Serial.println("[INF] SD ok");
  }

  //initialize buttons
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_VOL_UP, INPUT_PULLUP);
  pinMode(BTN_VOL_DOWN, INPUT_PULLUP);

  debouncerNext.attach(BTN_NEXT);
  debouncerNext.interval(5);
  debouncerPrev.attach(BTN_PREV);
  debouncerPrev.interval(5);
  debouncerVolUp.attach(BTN_VOL_UP);
  debouncerVolUp.interval(5);
  debouncerVolDown.attach(BTN_VOL_DOWN);
  debouncerVolDown.interval(5);

  printMemUsage();
}

void printMemUsage() {
  Serial.print("[DBG] memory = ");
  Serial.print(freeMemory());
  Serial.println(" byte");
}

void loop() {

  debouncerNext.update();
  debouncerPrev.update();
  debouncerVolUp.update();
  debouncerVolDown.update();

  long code = currentCode;

  if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
  {
    cardDeadCounter = 0;
    code = 0;
    for (byte i = 0; i < mfrc522.uid.size; i++)
    {
      code = ((code + mfrc522.uid.uidByte[i]) * 10);
    }

  } else {

    if (cardDeadCounter < 3) {
      cardDeadCounter++;
    }

    if (cardDeadCounter == 3  && currentCode != 0) {
      code = 0;
    }
  }

  if (code != currentCode) {
    currentCode = code;
    rfcChanged();
  }

  if ( debouncerNext.fell() ) {
    Serial.println("[USR] Next pressed");
    playNext();
  }
  if ( debouncerPrev.fell()) {
    Serial.println("[USR] Prev pressed");
    playPrev();
  }
  if ( debouncerVolUp.fell() ) {
    Serial.println("[USR] VolUp pressed");
    volumeUp();
  }
  if ( debouncerVolDown.fell()) {
    Serial.println("[USR] VolDown pressed");
    volumeDown();
  }

  automaticNext();

}

void automaticNext() {

  if (currentCode != 0 && musicPlayer.stopped()) {
    Serial.println("[SYS] Automatically play next file");
    playNext();
  }

}

String getFolderNameByCode(long code) {

  File rootFolder = SD.open("/");
  rootFolder.rewindDirectory();

  while (true) {
    File rootFolderEntry =  rootFolder.openNextFile();
    if (! rootFolderEntry) {
      // no more files
      break;
    }

    if (rootFolderEntry.isDirectory()) {

      String rfidFileName = String("/");
      rfidFileName += String(rootFolderEntry.name());
      rfidFileName += "/";
      rfidFileName += code;

      if (SD.exists(rfidFileName)) {
        rootFolderEntry.close();
        rootFolder.close();
        return String(rootFolderEntry.name());
      }
    }
    rootFolderEntry.close();
  }
  rootFolder.close();

  return String();
}

void fillTrackListByFolderName(String foldername) {
  trackList.clear();

  String absoluteFoldername = String("/");
  absoluteFoldername += foldername;

  File albumFolder = SD.open(absoluteFoldername);

  if (!albumFolder) {
    goError(String(absoluteFoldername) + " can not be opened.",2);
    delay(2000);
    return;
  }

  albumFolder.rewindDirectory();
  while (true) {
    File albumFolderEntry =  albumFolder.openNextFile();
    if (! albumFolderEntry) {
      // no more files
      break;
    }
    String entryName = String(albumFolderEntry.name());
    if (entryName.endsWith("MP3") || entryName.endsWith("OGG")) {
      trackList.add(entryName);
    }

    albumFolderEntry.close();
  }
  albumFolder.close();
  trackList.sort(fileSortCompare);

}

String getFilenameByIndices(long code, int track) {

  Serial.print("[INF] Search folder for code ");
  Serial.println(code);

  String foldername = getFolderNameByCode(code);

  String absoluteFolderName = String("/");
  absoluteFolderName += foldername;
  File albumFolder = SD.open(absoluteFolderName);
  albumFolder.rewindDirectory();

  if (!albumFolder) {
    goError(String(absoluteFolderName) + " can not be opened.",2);
    delay(2000);
    return String();
  }

  /*
  int  i = 0;
  while (true) {

    File albumFolderEntry =  albumFolder.openNextFile();
    if (! albumFolderEntry) {
      // no more files
      break;
    }

    if (String(albumFolderEntry.name()).endsWith("MP3") || String(albumFolderEntry.name()).endsWith("OGG")) {
      if (i == track) {
        absoluteFolderName += "/";
        absoluteFolderName += String(albumFolderEntry.name());
        albumFolderEntry.close();
        albumFolder.close();
        return absoluteFolderName;
      }
      i++;
    }

    albumFolderEntry.close();
  }
  */
  
  albumFolder.close();

  absoluteFolderName += "/";
  absoluteFolderName += trackList.get(track);
  return absoluteFolderName;
}

void rfcChanged() {

  if (currentCode == 0) {
    musicPlayer.stopPlaying();
    Serial.println("[USR] RFID removed");
    setColor(0, 186, 0);
  } else {
    setColor(0, 0, 255);
    unsigned long currentRfcChange = millis();

    //skip too much changes (happens with multiple tokens at the same time)
    if (currentRfcChange < lastRfcChange + RFC_MIN_INTERVAL && lastRfcChange != 0) {
      lastRfcChange = currentRfcChange;
      trackList.clear();
      currentCode = 0;
      currentTrack = 0;
      goError("No folder found.",3);
      delay(2000);
      setColor(0, 0, 255);
      return;
    }

    lastRfcChange = currentRfcChange;

    musicPlayer.stopPlaying();

    Serial.print("[USR] New RFID: ");
    Serial.println(currentCode);
    String rootFolder = getFolderNameByCode(currentCode);

    if (rootFolder.length() == 0) {
      trackList.clear();
      currentCode = 0;
      currentTrack = 0;
      goError("No folder found.",3);
      delay(2000);
      setColor(0, 0, 255);
      return;
    }

    Serial.print("[INF] Found folder ");
    Serial.print(rootFolder);
    Serial.println(".");

    fillTrackListByFolderName(rootFolder);

    if (trackList.size() == 0) {
      trackList.clear();
      currentCode = 0;
      currentTrack = 0;
      Serial.println("No tracks in track list");
      goError("No tracks in track list.",4);
      delay(2000);
      setColor(0, 0, 255);
      return;
    }

    Serial.print("[INF] Track list contains ");
    Serial.print(trackList.size());
    Serial.println(" elements.");

    //todo check
    playSpecific(0);
  }

}

void playSpecific(int track) {

  musicPlayer.stopPlaying();

  printMemUsage();

  Serial.print("Track ");
  Serial.print(track);
  Serial.print("|");
  Serial.println(trackList.get(track));

  String filename = getFilenameByIndices(currentCode, track);
  
  Serial.print("[INF] Play track: ");
  Serial.print(filename);
  Serial.print(" from code ");
  Serial.println(currentCode);

  if (filename.length() == 0) {
    goError("File not found. Can not play.",5);
      delay(2000);
    return;
  }

  Serial.print("[INF] Found track file: ");
  Serial.println(filename);

  printMemUsage();

  currentTrack = track;

  char playFileName[filename.length() + 1];
  filename.toCharArray(playFileName, filename.length() + 1);

  musicPlayer.startPlayingFile(playFileName);
}

void playNext() {

  if (trackList.size() - 1 > currentTrack) {
    playSpecific(currentTrack + 1);
  } else {
    playSpecific(0);
  }
}

void playPrev() {
  if (0 < currentTrack) {
    playSpecific(currentTrack - 1);
  } else {
    playSpecific(trackList.size() - 1);
  }
}

void volumeUp() {
  if (currentVolume > 0) {
    currentVolume -= 10;
    musicPlayer.setVolume(currentVolume, currentVolume);
    Serial.print("[INF] Volume ");
    Serial.println(currentVolume);
    if (musicPlayer.stopped() || musicPlayer.paused()) {
      musicPlayer.sineTest(0x44, 500);
    }
  }
}

void volumeDown() {
  if (currentVolume < MAX_VOL) {
    currentVolume += 10;
    musicPlayer.setVolume(currentVolume, currentVolume);
    Serial.print("[INF] Volume ");
    Serial.println(currentVolume);
    if (musicPlayer.stopped() || musicPlayer.paused()) {
      musicPlayer.sineTest(0x44, 500);
    }
  }
}



void setColor(int red, int green, int blue)
{
#ifdef COMMON_ANODE
  red = 255 - red;
  green = 255 - green;
  blue = 255 - blue;
#endif
  analogWrite(RED_LED, red);
  analogWrite(GREEN_LED, green);
  analogWrite(BLUE_LED, blue);
}

int fileSortCompare(String a, String b) {
  return a.compareTo(b);
}



