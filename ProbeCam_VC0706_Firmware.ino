#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>

//#define DEBUG

// Pins
#define CAM_ON 4
#define ARDUINO_TX 2
#define ARDUINO_RX 3
#define LED 7

// Camera registers
#define VC0706_RESET  0x26
#define VC0706_GEN_VERSION 0x11
#define VC0706_SET_PORT 0x24
#define VC0706_READ_FBUF 0x32
#define VC0706_GET_FBUF_LEN 0x34
#define VC0706_FBUF_CTRL 0x36
#define VC0706_DOWNSIZE_CTRL 0x54
#define VC0706_DOWNSIZE_STATUS 0x55
#define VC0706_READ_DATA 0x30
#define VC0706_WRITE_DATA 0x31
#define VC0706_COMM_MOTION_CTRL 0x37
#define VC0706_COMM_MOTION_STATUS 0x38
#define VC0706_COMM_MOTION_DETECTED 0x39
#define VC0706_MOTION_CTRL 0x42
#define VC0706_MOTION_STATUS 0x43
#define VC0706_TVOUT_CTRL 0x44
#define VC0706_OSD_ADD_CHAR 0x45

#define VC0706_STOPCURRENTFRAME 0x0
#define VC0706_STOPNEXTFRAME 0x1
#define VC0706_RESUMEFRAME 0x3
#define VC0706_STEPFRAME 0x2

#define VC0706_640x480 0x00
#define VC0706_320x240 0x11
#define VC0706_160x120 0x22

#define VC0706_MOTIONCONTROL 0x0
#define VC0706_UARTMOTION 0x01
#define VC0706_ACTIVATEMOTION 0x01

#define VC0706_SET_ZOOM 0x52
#define VC0706_GET_ZOOM 0x53

#define CAMERABUFFSIZ 100
#define CAMERADELAY 10

// Camera low level function variables
uint8_t  serialNum;
uint8_t  camerabuff[CAMERABUFFSIZ + 1];
uint8_t  bufferLen;
uint16_t frameptr;

SoftwareSerial arduino(ARDUINO_TX, ARDUINO_RX);

int questionPositions[50];

String incomingCommand;

File myFile;

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(CAM_ON, OUTPUT);
  digitalWrite(CAM_ON, HIGH);
  delay(2000);

  Serial.begin(38400);

  arduino.begin(9600);

  // Initialise SD card
  pinMode(10, OUTPUT);
  if (!SD.begin(10)) {
    digitalWrite(LED, HIGH);
    arduino.println(-2);
    while (1);
  }

  // Success LED
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED, HIGH);
    delay(50);
    digitalWrite(LED, LOW);
    delay(50);
  }

  cam_reset();
  delay(2000);
  char *reply = cam_getVersion();
#ifdef DEBUG
  arduino.print("Camera version: ");
  arduino.println(reply);
#endif

  delay(2000);
  cam_setImageSize(VC0706_640x480);

  arduino.write('R');
}

void loop()
{
  while (arduino.available() > 0) {
    char inByte = arduino.read();
    //arduino.write(inByte);
    if (inByte == 0x0A) {
      processCommand(incomingCommand);
      incomingCommand = "";
    }
    else incomingCommand += (char)inByte;
  }
}

// Process incoming command from the Arduino.
void processCommand(String cmd) {
  if (cmd.charAt(0) == '?' ) arduino.println(getQuestion(cmd.substring(1).toInt()));
  else if (cmd.charAt(0) == '!') takePicture(cmd.substring(1).toInt());
  else if (cmd.charAt(0) == 'A') answerQuestion(cmd.substring(1).toInt());
  else if (cmd.charAt(0) == '$') arduino.println(getNumOfQuestions());
  else if (cmd.charAt(0) == 'S') sleep();
}

// Retrieve a question.
String getQuestion(int q) {
  String question;
  int currentQuestion = -1;

  myFile = SD.open("q.txt");

  if (!myFile) return "No questions found.";

  do {
    question = myFile.readStringUntil('\n');
    currentQuestion++;
  } while (currentQuestion != q);

  myFile.close();

  return question;
}

// Retrieve the total number of questions in the SD.
int getNumOfQuestions() {
  int result = 0;

  myFile = SD.open("q.txt");

  if (!myFile) return -1;

  //
  while (myFile.available()) {
    char inChar = myFile.read();
    if (inChar == '\n') {
      result++;
      questionPositions[result] = myFile.position();
    }
  }
  myFile.close();

  return result + 1;
}

// Mark a question as completed.
void answerQuestion(int q) {
  myFile = SD.open("q.txt", FILE_WRITE);
  char thisChar;
  char nextChar;

  myFile.seek(questionPositions[q]);
  nextChar = myFile.peek();
  myFile.print("#");

  while (myFile.available()) {
    thisChar = nextChar;
    nextChar = myFile.peek();
    myFile.print(thisChar);
  }
  myFile.close();

  // Call this to reindex the questions.
  getNumOfQuestions();
}

// Take a picture.
void takePicture(int q) {

  // Construct file name
  char filename[13];
  strcpy(filename, "Q00-00.JPG");
  for (int i = 0; i < 100; i++) {
    filename[1] = '0' + q / 10;
    filename[2] = '0' + q % 10;
    filename[4] = '0' + i / 10;
    filename[5] = '0' + i % 10;
    if (!SD.exists(filename)) {
      break;
    }
  }

  captureAndSave(filename);

  answerQuestion(q);

  digitalWrite(7, HIGH);
  delay(100);
  digitalWrite(7, LOW);
  delay(400);
  digitalWrite(7, HIGH);
  delay(100);
  digitalWrite(7, LOW);
  delay(400);
  digitalWrite(7, HIGH);
  delay(100);
  digitalWrite(7, LOW);
  delay(400);

  //disableCamera();

  arduino.println("R");
}

void captureAndSave(String filename) {

  // Take picture.

  if (!cam_takePicture()) {
#ifdef DEBUG
    arduino.println("Failed to take picture.");
#endif
  }
  else {
#ifdef DEBUG
    arduino.println("Picture taken.");
#endif
  }

  // Open file.
  File imgFile = SD.open(filename, FILE_WRITE);

  // Request JPEG length.
  uint16_t jpglen = cam_frameLength();
  uint16_t totalLength = jpglen;
  int percentDone = 0;
  int prevPercentDone = -1;
#ifdef DEBUG
  arduino.print("JPEG length: ");
  arduino.println(totalLength);
#endif

  // Write image data from camera to SD card.
  int32_t time = millis();
  pinMode(8, OUTPUT);

  byte wCount = 0;
  while (jpglen > 0) {
    // Read 64 bytes at a time;
    uint8_t *buffer;
    uint8_t bytesToRead = min(64, jpglen);
    buffer = cam_readPicture(bytesToRead);
    imgFile.write(buffer, bytesToRead);
    jpglen -= bytesToRead;
    percentDone = (float)(totalLength - jpglen) / totalLength * 100;
    if (percentDone != prevPercentDone) arduino.println(percentDone);
    prevPercentDone = percentDone;
  }

  // Close file.
  imgFile.close();

#ifdef DEBUG
  // Measure save time.
  time = millis() - time;
  arduino.println("done!");
  arduino.print(time); arduino.println(" ms elapsed");
#endif
}


void sleep() {
  digitalWrite(LED, HIGH);
  delay(10);
  digitalWrite(LED, LOW);
  //disableADC();
  //enableSleepMode();
  //disableBOD();
  //__asm__ __volatile__("sleep");
}

void disableCamera() {
  digitalWrite(CAM_ON, LOW);
  digitalWrite(0, LOW);
  digitalWrite(1, LOW);
}

void enableCamera() {
  digitalWrite(CAM_ON, HIGH);
}

