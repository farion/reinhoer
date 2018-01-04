//#define DEBUG

#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h> // lib for player 
#include <MFRC522.h> // lib for rfid
#include <Bounce2.h>
#ifdef DEBUG
#include "src/MemoryFree/MemoryFree.h"
#endif

/**
   ERRORS

   1x beep => SD failed.
   2x beep => Folder can not be opened.
   3x beep => Folder not found.
   4x beep => No tracks in folder.
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

#define COLOR_OFF 0
#define COLOR_RED 1
#define COLOR_BLUE 2
#define COLOR_GREEN 3
#define COLOR_WHITE 4
#define COLOR_PURPLE 5

#define VOL_INIT 70 // Initial volume (0-100)
#define MIN_VOL 100 // Minimal volume (0-100) inverted
#define MAX_VOL 0 // Maximal volume (0-100) inverted

#define RFC_MIN_INTERVAL 3000 // Interval in milliseconds between two RFID changes. Leads to less flickering between multiple RFID tokens.

#define VOLUME_DIR "VOLUME"

#define MAX_VOL_FILE "/MAXVOL"
#define CUR_VOL_FILE "/CURVOL"

#define NO_REPEAT_FILE "NOREPEAT"

#define FEEDBACK_TIME 200

#define DEAD_CARD_COUNTER 20

//#define BLINK_INTERVAL 1000

MFRC522 mfrc522(RFC_CS, RFC_RESET);

Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(VS_RESET, VS_CS, VS_DCS, VS_DREQ, SD_CS);

long lastCode = 0;
long currentCode = 0;
String currentTrack = "";
long cardDeadCounter = 0;
int currentVolume = VOL_INIT;
unsigned long lastRfcChange = -RFC_MIN_INTERVAL;
String rootFolder;
int currentMaxVolume;
bool repeatCurrentAlbum = true;

int ledColor = COLOR_OFF;
long resetColor = 0;

bool saveMaxVolumeRequest = false;
bool saveCurrentVolumeRequest = false;

Bounce debouncerNext = Bounce();
Bounce debouncerPrev = Bounce();
Bounce debouncerVolUp = Bounce();
Bounce debouncerVolDown = Bounce();

#ifdef DEBUG
void goError(String message, int beepCount) {
#else
void goError(int beepCount) {
#endif
  
  for (int i = 0; i < beepCount; i++) {
    setColor(186, 0, 0);
    musicPlayer.sineTest(0x44, 300);
    delay(1000);
    musicPlayer.stopPlaying();
    setColor(0, 0, 0);
  }
  #ifdef DEBUG
  Serial.print("[ERR] ");
  Serial.println(message);
  #endif
}

void setup() {
  #ifdef DEBUG
  Serial.begin(9600);
  delay(3000);
  #endif

  //initialize LED
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  //set LED to red
  setColor(0, 0, 186);

  SPI.begin();

  mfrc522.PCD_Init();

  // initialize the music player
  if (!musicPlayer.begin()) {
    setColor(186, 0, 0); // Set LED to red
    #ifdef DEBUG
    Serial.println("[ERR] VS1053 not found");
    #endif
    exit(1);
  } 
  #ifdef DEBUG
  else {
    Serial.println("[INF] VS1053 found");
  }
  #endif
  
  //initialize SD card
  if (!SD.begin(SD_CS)) {
    #ifdef DEBUG
    goError("SD failed", 1);
    #else
    goError(1);
    #endif
        exit(1);
  } else {
    #ifdef DEBUG
    Serial.println("[INF] SD ok");
    #endif
  }
    
  currentMaxVolume = getMaximalVolume();
  int initial_volume = getInitialVolume();
  int volume_intial = max(getInitialVolume(),currentMaxVolume);

  musicPlayer.setVolume(volume_intial, volume_intial); // Set initial volume
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); // DREQ int

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

  #ifdef DEBUG
  printDebug();
  #endif

  setColor(0, 186, 0);
}

int getInitialVolume(){
  return readIntFromFile(CUR_VOL_FILE,VOL_INIT);
}

int getMaximalVolume(){
  return readIntFromFile(MAX_VOL_FILE,MAX_VOL);
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

    if (cardDeadCounter < DEAD_CARD_COUNTER) {
      cardDeadCounter++;
    }

    if (cardDeadCounter == DEAD_CARD_COUNTER  && currentCode != 0) {
      code = 0;
    }
  }
  
  if (code != currentCode){
    unsigned long currentRfcChange = millis();

    bool timelimit = currentRfcChange > lastRfcChange + RFC_MIN_INTERVAL;
    if(timelimit || code == 0 || lastCode == code) {
      if(code == 0){
        lastCode = currentCode;
      }else{
        lastRfcChange = currentRfcChange;  
      }
      currentCode = code;
      rfcChanged();
    }else {
      if(!timelimit){
        setColor(200, 0, 0); 
        delay(RFC_MIN_INTERVAL);
        setColor(0, 186, 0); 
      }
    }
  }

  if(resetColor != 0 && resetColor < millis()){
    setColor(ledColor);
    resetColor = 0;
  }

  if (debouncerNext.fell() ) {
    if(!rootFolder.equals(VOLUME_DIR)){
      #ifdef DEBUG
      Serial.println("[USR] Next pressed");    
      #endif
      startResetColor(COLOR_PURPLE,millis() + FEEDBACK_TIME);
      playNext();
    }
  }
  if (debouncerPrev.fell()) {
    if(!rootFolder.equals(VOLUME_DIR)){
      #ifdef DEBUG
      Serial.println("[USR] Prev pressed");
      #endif
      startResetColor(COLOR_PURPLE,millis() + FEEDBACK_TIME);
      playPrev();
    }
  }
  if ( debouncerVolUp.fell()) {
    #ifdef DEBUG
    Serial.println("[USR] VolUp pressed");
    #endif
    startResetColor(COLOR_PURPLE,millis() + FEEDBACK_TIME);
    volumeUp();
  }
  if ( debouncerVolDown.fell()) {
    #ifdef DEBUG
    Serial.println("[USR] VolDown pressed");
    #endif
    startResetColor(COLOR_PURPLE,millis() + FEEDBACK_TIME);
    volumeDown();
  }

  if(saveMaxVolumeRequest && musicPlayer.stopped()){
    saveMaxVolume();
  }

  if(saveCurrentVolumeRequest && musicPlayer.stopped()){
    saveCurrentVolume();
  }
  
  automaticNext();
}

void automaticNext() {

  if (currentCode != 0 && musicPlayer.stopped()) {
    #ifdef DEBUG
    Serial.println("[SYS] Automatically play next file");
    #endif
    playNext();
  }
}

void rfcChanged() {

  if (currentCode == 0) {
    musicPlayer.stopPlaying();
    #ifdef DEBUG
    Serial.println("[USR] RFID removed");
    #endif
    setMainColor(COLOR_GREEN);
  } else {
    setMainColor(COLOR_BLUE);

    musicPlayer.stopPlaying();

    #ifdef DEBUG
    Serial.print("[USR] New RFID: ");
    Serial.println(currentCode);
    #endif

    if(currentCode != lastCode){
      currentTrack = "";
    }
    #ifdef DEBUG
    else{
      Serial.println("[INF] Same RFID as before, do not clear state.");
    }
    #endif
    
    rootFolder = getFolderNameByCode(currentCode);

    repeatCurrentAlbum = isRepeat();

    if (rootFolder.length() == 0) {
      currentCode = 0;
      currentTrack = "";
      #ifdef DEBUG
      goError("No folder found.", 3);
      #else
      goError(3);
      #endif
      delay(2000);
      return;
    }

    #ifdef DEBUG
    Serial.print("[INF] Found folder ");
    Serial.print(rootFolder);
    Serial.print(" with currentCode ");
    Serial.print(currentCode);
    Serial.println(".");
    #endif

    playCurrent();
  }
}

bool isRepeat(){
  String absoluteNoRepeatFileName = String("/");
  absoluteNoRepeatFileName += rootFolder;
  absoluteNoRepeatFileName += "/";
  absoluteNoRepeatFileName += NO_REPEAT_FILE;

  File noRepeatFile = SD.open(absoluteNoRepeatFileName,FILE_READ);
  if(!noRepeatFile){
    return true;
  }
  noRepeatFile.close();
  return false;
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

void playCurrent(){

  musicPlayer.stopPlaying();

  String absoluteFoldername = String("/");
  absoluteFoldername += rootFolder;

  if(currentTrack.equals("")){
    playNext();
    return;
  }

  playCurrentTrack();
}

void playPrev(){
  playDirectSibling(-1);
}

void playNext(){
  playDirectSibling(1);
}

void playDirectSibling(int modificator){

  musicPlayer.stopPlaying();
  String absoluteFoldername = String("/");
  absoluteFoldername += rootFolder;

  File albumFolder = SD.open(absoluteFoldername);

  if (!albumFolder) {
    #ifdef DEBUG
    goError(String(absoluteFoldername) + " can not be opened.", 2);
    #else
    goError(2);
    #endif    
    delay(2000);
    return;
  }

  albumFolder.rewindDirectory();

  String candidateTrack = "";
  String defaultTrack = "";
  
  while (true) {
    File albumFolderEntry =  albumFolder.openNextFile();
    if (! albumFolderEntry) {
      break;
    }
    String entryName = String(albumFolderEntry.name());

    if(entryName.endsWith(".MP3")){

      if(defaultTrack.equals("") || modificator * defaultTrack.compareTo(entryName) > 0){
         defaultTrack = entryName;
      }
      
      if(!currentTrack.equals("")){
        if(candidateTrack.equals("")){
          if(modificator * entryName.compareTo(currentTrack) > 0){
            candidateTrack = entryName;
          }
        }else{
          if(modificator * entryName.compareTo(candidateTrack) < 0 && modificator * entryName.compareTo(currentTrack) > 0){
            candidateTrack = entryName;
          }
        }
      }
    }

    albumFolderEntry.close();
  }

  albumFolder.close();

  if(candidateTrack.equals("")){
    if(repeatCurrentAlbum){
      currentTrack = defaultTrack;
    }
  }else{  
    currentTrack = candidateTrack;
  }

  if(currentTrack.equals("")){
    #ifdef DEBUG
    goError("No tracks in folder",4);
    #else
    goError(4);
    #endif
  }else{
    playCurrentTrack();
  }
}

void playCurrentTrack(){
  
  String absoluteFileName = String("/");
  absoluteFileName += rootFolder;;
  absoluteFileName += "/";
  absoluteFileName += currentTrack;

  #ifdef DEBUG
  Serial.print("[INF] Play ");
  Serial.println(absoluteFileName);
  #endif

  char playFileName[absoluteFileName.length() + 1];
  
  absoluteFileName.toCharArray(playFileName, absoluteFileName.length() + 1);
  if(!musicPlayer.startPlayingFile(playFileName)){
    #ifdef DEBUG
    goError("Can not play file",5);
    #else
    goError(5);
    #endif
  }

  #ifdef DEBUG
  printDebug();
  #endif
}

void volumeUp() {
  if (currentVolume > currentMaxVolume || (rootFolder.equals(VOLUME_DIR) && currentVolume > 0)) {
    currentVolume -= 10;
    musicPlayer.setVolume(currentVolume, currentVolume);
    #ifdef DEBUG
    Serial.print("[INF] Volume ");
    Serial.println(currentVolume);
    #endif
    saveVolume();
  }
}

void volumeDown() {
  if (currentVolume < MIN_VOL) {
    currentVolume += 10;
    musicPlayer.setVolume(currentVolume, currentVolume);
    #ifdef DEBUG
    Serial.print("[INF] Volume ");
    Serial.println(currentVolume);
    #endif
    saveVolume();
  }
}

void saveVolume(){
  if(rootFolder.equals(VOLUME_DIR)){
    currentMaxVolume = currentVolume;
    saveMaxVolumeRequest = true;
  }
  saveCurrentVolumeRequest = true;
}

void saveMaxVolume(){
  writeIntToFile(MAX_VOL_FILE,currentVolume); 
  saveMaxVolumeRequest = false;
}

void saveCurrentVolume(){
  writeIntToFile(CUR_VOL_FILE,currentVolume);
  saveCurrentVolumeRequest = false;
}

int readIntFromFile(String filename, int def){
  #ifdef DEBUG
  Serial.print("[INF] Read from file ");
  Serial.println(filename);
  if(SD.exists(filename)){
    Serial.println("[INF] File exists");
  }
  #endif
  File file = SD.open(filename,FILE_READ);
  if(file){
    char result[3];
    uint8_t i = 0;
    do {
      result[i] = file.read();
      i++;
    }while(i < 3 && result[i] != '\0');
    file.close();

    int value = atoi(result);

    #ifdef DEBUG
    Serial.print("[INF] Read Value ");
    Serial.println(value);
    #endif
    
    return  value;
  }

  #ifdef DEBUG
  Serial.print("[INF] Use default ");
  Serial.println(def);
  #endif
  
  return def;
}

void writeIntToFile(String filename, int i){
  if(SD.exists(filename)){
    SD.remove(filename);
  }

  File file = SD.open(filename, FILE_WRITE);
  #ifdef DEBUG
  Serial.print("[INF] Write to file ");
  Serial.print(filename);
  Serial.print(" value ");
  Serial.println(i);
  #endif
  file.println(i);
  file.close();
}

void startResetColor(int color, long dela){
      setColor(color);
      resetColor = dela;
}

void setMainColor(int color){
  setColor(color);
  
  ledColor = color;
  resetColor = 0;
}

void setColor(int color){
  if(color == COLOR_OFF){
     setColor(0,0,0);
  }else if(color == COLOR_RED){
    setColor(200,0,0);
  }else if(color == COLOR_BLUE){
    setColor(0,0,200);
  }else if(color == COLOR_GREEN){
    setColor(0,200,0);
  }else if(color == COLOR_WHITE){
    setColor(200,200,200);
  }else if(color == COLOR_PURPLE){
    setColor(200,0,200);
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


#ifdef DEBUG
void printDebug() {
  Serial.print("[DBG] memory = ");
  Serial.print(freeMemory());
  Serial.print(" byte / currentTrack = ");
  Serial.print(currentTrack);
  Serial.print(" / currentCode = ");
  Serial.print(currentCode);
  Serial.print(" / lastCode = ");
  Serial.print(lastCode);
  Serial.print(" / rootFolder = ");
  Serial.println(rootFolder);
}
#endif




