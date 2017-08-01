#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <MFRC522.h> // RFID Bibliothek Laden
#include <Bounce2.h>
#include <LinkedList.h>
#include "src/MemoryFree/MemoryFree.h"

#define COMMON_ANODE //for LED

#define RESET 13 // VS1053 reset pin (output)
#define CS 10 // VS1053 chip select pin (output)
#define DCS 8 // VS1053 Data/command select pin (output)
#define DREQ 3 // VS1053 Data request pin (into Arduino)
#define CARDCS 6 // Card chip select pin

#define RFC_CS 2 // CS for RFC
#define RFC_RESET 11 //RESET for RFC

#define BTN_NEXT 4
#define BTN_PREV 13
#define BTN_VOL_UP 12
#define BTN_VOL_DOWN 5

#define RED_LED A2
#define GREEN_LED A1
#define BLUE_LED A0

#define VOL_INIT 40

#define RFC_MIN_INTERVAL 1000
/**
   ERRORS

   1x piep => Player Failed
   2x piep => SD Card Failed
   3x piep => No config file on SD
*/

MFRC522 mfrc522(RFC_CS, RFC_RESET);

Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(RESET, CS, DCS, DREQ, CARDCS);

long currentCode = 0;
int currentTrack = 0;
LinkedList<int> trackList = LinkedList<int>();
long cardDeadCounter = 0;
int currentVolume = VOL_INIT;

Bounce debouncerNext = Bounce();
Bounce debouncerPrev = Bounce();
Bounce debouncerVolUp = Bounce();
Bounce debouncerVolDown = Bounce();

const size_t bufferLen = 80;

unsigned long lastRfcChange = 0;

void setup() {
  Serial.begin(9600);

  /*  while (!Serial) {
      ; // wait for serial port to connect. Needed for Leonardo only
    }
  */
  SPI.begin();

  mfrc522.PCD_Init();


  // initialise the music player
  if (!musicPlayer.begin()) {
    //musicPlayer.sineTest(0x44, 300);
    Serial.println("[ERR] VS1053 not found");
  } else {
    Serial.println("[INF] VS1053 found");
  }

  if (!SD.begin(CARDCS)) {
    //musicPlayer.sineTest(0x44, 300);
    //musicPlayer.sineTest(0x44, 300);
    Serial.println("[ERR] SD failed");
  } else {
    Serial.println("[INF] SD ok");
  }

  musicPlayer.setVolume(VOL_INIT, VOL_INIT);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); // DREQ int

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

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  setColor(0, 186, 0);

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
    Serial.print(absoluteFoldername);
    Serial.println(" can not be opened.");
    return;
  }

  albumFolder.rewindDirectory();
  int  i = 0;
  while (true) {
    File albumFolderEntry =  albumFolder.openNextFile();
    if (! albumFolderEntry) {
      // no more files
      break;
    }
    if (String(albumFolderEntry.name()).endsWith("MP3") || String(albumFolderEntry.name()).endsWith("OGG")) {
      trackList.add(i);
      i++;
    }

    albumFolderEntry.close();
  }
  albumFolder.close();

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
    Serial.print("[ERR]");
    Serial.print(absoluteFolderName);
    Serial.println(" can not be opened.");
    return String();
  }
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
  albumFolder.close();

  return String();
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
      Serial.println("No folder found");
      setColor(255, 0, 0);
      delay(1000);
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
      Serial.println("No folder found");
      setColor(255, 0, 0);
      delay(1000);
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
      setColor(255, 0, 0);
      delay(1000);
      setColor(0, 0, 255);
      return;
    }

    Serial.print("[INF] Track list contains ");
    Serial.print(trackList.size());
    Serial.println(" elements.");

    //todo check
    playSpecific(trackList.get(0));
  }

}

void playSpecific(int track) {

  musicPlayer.stopPlaying();

  printMemUsage();

  Serial.print("[INF] Play track: ");
  Serial.print(track);
  Serial.print(" from code ");
  Serial.println(currentCode);

  String filename = getFilenameByIndices(currentCode, track);

  if (filename.length() == 0) {
    Serial.println("[ERR] File not found. Can not play");
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
  if (currentVolume < 100) {
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


