/*
  Spacetime using ESP32

  Modified by Tiffany Tseng for esp32 Arduino Board Definition 3.0+ 
  Originally created by Mark Santolucito for Barnard COMS 3930
  Based on DroneBot Workshop 2022 ESP-NOW Multi Unit Demo
*/

// Include Libraries
#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

String cmd1 = "";
String cmd2 = "";
volatile bool scheduleCmd1Send = false;
volatile bool scheduleCmd2Send = false;

String cmdRecvd = "";
const String waitingCmd = "Wait for cmds";
bool redrawCmdRecvd = false;

// for drawing progress bars
int progress = 0;
bool redrawProgress = true;
int lastRedrawTime = 0;

//we could also use xSemaphoreGiveFromISR and its associated fxns, but this is fine
volatile bool scheduleCmdAsk = true;
hw_timer_t *askRequestTimer = NULL;
volatile bool askExpired = false;
hw_timer_t *askExpireTimer = NULL;
int expireLength = 25;

#define ARRAY_SIZE 10
const String commandVerbs[ARRAY_SIZE] = { "Buzz", "Engage", "Floop", "Bother", "Twist", "Jingle", "Jangle", "Yank", "Press", "Play" };
const String commandNounsFirst[ARRAY_SIZE] = { "foo", "dev", "bobby", "jaw", "tooty", "wu", "fizz", "rot", "tea", "bee" };
const String commandNounsSecond[ARRAY_SIZE] = { "bars", "ices", "pins", "nobs", "zops", "tangs", "bells", "wels", "pops", "bops" };

int lineHeight = 30;

volatile bool nameScreen = true;
// Define user name vars
String userName = "___";          
int currentLetterIndex = 0;         
char selectedLetters[3] = {'A', 'A', 'A'}; // Array to store the selected letters

int room[4] = {0, 0, 0, 0};
int curr_highlight = 0;

volatile bool roomScreen = false; 

#define SHORT_PRESS_TIME 500 // 500 milliseconds
#define LONG_PRESS_TIME  3000 // 3000 milliseconds
// Variables will change:
int lastLeftState = LOW;  // the previous state from the input pin
// int currentLeftState;     // the current reading from the input pin
int lastRightState = LOW;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

volatile bool teamScreen = false;

volatile bool gameScreen = false;

volatile bool endScreen = false;

volatile bool drawControlsOut = true;

// Define LED and pushbutton pins
#define BUTTON_LEFT 0
#define BUTTON_RIGHT 35


void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
// Formats MAC Address
{
  snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}


void receiveCallback(const esp_now_recv_info_t *macAddr, const uint8_t *data, int dataLen)
/* Called when data is received
   You can receive 3 types of messages
   1) a "ASK" message, which indicates that your device should display the cmd if the device is free
   2) a "DONE" message, which indicates the current ASK? cmd has been executed
   3) a "PROGRESS" message, indicating a change in the progress of the spaceship
   
   Messages are formatted as follows:
   [A/D]: cmd
   For example, an ASK message for "Twist the wutangs":
   A: Twist the wutangs
   For example, a DONE message for "Engage the devnobs":
   D: Engage the devnobs
   For example, a PROGESS message for 75% progress
   P: 75
*/

{
  // Only allow a maximum of 250 characters in the message + a null terminating byte
  char buffer[ESP_NOW_MAX_DATA_LEN + 1];
  int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
  strncpy(buffer, (const char *)data, msgLen);

  // Make sure we are null terminated
  buffer[msgLen] = 0;
  String recvd = String(buffer);
  Serial.println(recvd);
  // Format the MAC address
  char macStr[18];
  // formatMacAddress(macAddr, macStr, 18);

  // Send Debug log message to the serial port
  Serial.printf("Received message from: %s \n%s\n", macStr, buffer);
  if (recvd[0] == 'A' && cmdRecvd == waitingCmd && random(100) < 30)  //only take an ask if you don't have an ask already and only take it XX% of the time
  {
    recvd.remove(0, 3);
    cmdRecvd = recvd;
    redrawCmdRecvd = true;
    timerStart(askExpireTimer);  //once you get an ask, a timer starts
  } else if (recvd[0] == 'D' && recvd.substring(3) == cmdRecvd) {
    timerWrite(askExpireTimer, 0);
    timerStop(askExpireTimer);
    cmdRecvd = waitingCmd;
    progress = progress + 1;
    broadcast("P: " + String(progress));
    redrawCmdRecvd = true;

  } else if (recvd[0] == 'P') {
    recvd.remove(0, 3);
    progress = recvd.toInt();
    redrawProgress = true;
  }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);
  Serial.print("Last Packet Sent to: ");
  Serial.println(macStr);
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void broadcast(const String &message)
// Emulates a broadcast
{
  // Broadcast a message to every device in range
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo);
  }
  // Send message
  esp_err_t result = esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());
}

void IRAM_ATTR sendCmd1() {
  scheduleCmd1Send = true;
}

void IRAM_ATTR sendCmd2() {
  scheduleCmd2Send = true;
}

void IRAM_ATTR onAskReqTimer() {
  scheduleCmdAsk = true;
}

void IRAM_ATTR onAskExpireTimer() {
  askExpired = true;
  timerStop(askExpireTimer);
  timerWrite(askExpireTimer, 0);
}

void espnowSetup() {
  // Set ESP32 in STA mode to begin with
  delay(500);
  WiFi.mode(WIFI_STA);
  Serial.println("ESP-NOW Broadcast Demo");

  // Print MAC address
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Disconnect from WiFi
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESP-NOW Init Success");
    esp_now_register_recv_cb(receiveCallback);
    esp_now_register_send_cb(sentCallback);
  } else {
    Serial.println("ESP-NOW Init Failed");
    delay(3000);
    ESP.restart();
  }
}

void buttonSetup() {
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);

  // attachInterrupt(digitalPinToInterrupt(BUTTON_LEFT), sendCmd1, FALLING);
  // attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), sendCmd2, FALLING);
}

void textSetup() {
  tft.init();
  tft.setRotation(0);

  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  // drawControls();

  cmdRecvd = waitingCmd;
  redrawCmdRecvd = true;
}

void timerSetup() {
  // https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/api/timer.html
  askRequestTimer = timerBegin(1000000); // 1MHz
  timerAttachInterrupt(askRequestTimer, &onAskReqTimer);
  timerAlarm(askRequestTimer, 5 * 1000000, true, 0);  //send out an ask every 5 secs

  askExpireTimer = timerBegin(80000000);
  timerAttachInterrupt(askExpireTimer, &onAskExpireTimer);
  timerAlarm(askExpireTimer, expireLength * 1000000, true, 0);
  timerStop(askExpireTimer);
}
void setup() {
  Serial.begin(115200);

  textSetup();
  buttonSetup();
  espnowSetup();
  timerSetup();

  drawNameEntryScreen(); 
}

String genCommand() {
  String verb = commandVerbs[random(ARRAY_SIZE)];
  String noun1 = commandNounsFirst[random(ARRAY_SIZE)];
  String noun2 = commandNounsSecond[random(ARRAY_SIZE)];
  return verb + " " + noun1 + noun2;
}

void drawControls() {

  cmd1 = genCommand();
  cmd2 = genCommand();
  cmd1.indexOf(' ');
  tft.drawString("B1: " + cmd1.substring(0, cmd1.indexOf(' ')), 0, 90, 2);
  tft.drawString(cmd1.substring(cmd1.indexOf(' ') + 1), 0, 90 + lineHeight, 2);
  tft.drawString("B2: " + cmd2.substring(0, cmd2.indexOf(' ')), 0, 170, 2);
  tft.drawString(cmd2.substring(cmd2.indexOf(' ') + 1), 0, 170 + lineHeight, 2);
}

void drawNameEntryScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Enter Name:", 10, 30, 1);

  for (int i = 0; i < 3; i++) {
    if (i == currentLetterIndex) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tft.drawChar(selectedLetters[i], 40 + i * 30, 70, 2);
  }
}


void handleNameEntry() {
  static String lastName = ""; // track the last drawn name to avoid redundant updates
  static int lastLetterIndex = -1;

  int currentLeftState = digitalRead(BUTTON_LEFT);

  if (lastLeftState == HIGH && currentLeftState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      // ledcWrite(0, 0);
      // digitalWrite(MOTOR_PIN, LOW);
        selectedLetters[currentLetterIndex]++;
        if (selectedLetters[currentLetterIndex] > 'Z') {
            selectedLetters[currentLetterIndex] = 'A';
        }
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      // don't do any action
      Serial.println("A long press is detected");
    }
  }

  lastLeftState = currentLeftState;

  int currentRightState = digitalRead(BUTTON_RIGHT);

  if (lastRightState == HIGH && currentRightState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastRightState == LOW && currentRightState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      // ledcWrite(0, 0);
      // digitalWrite(MOTOR_PIN, LOW);
        if (currentLetterIndex < 2){
          currentLetterIndex++; // move to next digit
        }
        else {
          currentLetterIndex = 0;
        }
      Serial.println("A short press is detected");
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      // move to room selection screen
      nameScreen = false;
      roomScreen = true;
      tft.fillScreen(TFT_BLACK);
      Serial.println("A long press is detected");
      lastRightState = currentRightState;
      return;
    }
  }
  lastRightState = currentRightState;   
    

    // update the userName and redraw if name or current letter index changes
    String newName = String(selectedLetters[0]) + String(selectedLetters[1]) + String(selectedLetters[2]);
    if (newName != lastName || currentLetterIndex != lastLetterIndex) {
        drawNameEntryScreen();
        lastName = newName;
        lastLetterIndex = currentLetterIndex;
    }
}



void drawRoom() {
  // tft.fillScreen(TFT_BLACK);
  // tft.setTextDatum(MR_DATUM);
  tft.drawString("Room Num", tft.width()/4 - 20, 30, 2);

  int currentLeftState = digitalRead(BUTTON_LEFT);

  // Detect left button press
  // Short Press: Change number by incrementation (0-9, wraparound)
  // Long Press: Go back to name screen
  if (lastLeftState == HIGH && currentLeftState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      room[curr_highlight] = (room[curr_highlight] + 1) % 10;
      Serial.println("A short press is detected");
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      tft.fillScreen(TFT_BLACK);
      Serial.println("A long press is detected");
      nameScreen = true;
      roomScreen = false;
      lastLeftState = currentLeftState;
      return;
    }
  }

  lastLeftState = currentLeftState;

  int currentRightState = digitalRead(BUTTON_RIGHT);

  // Detect right button press
  // Short Press: Change character position to change 
  // Long Press: Go forward to team screen
  if (lastRightState == HIGH && currentRightState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastRightState == LOW && currentRightState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      curr_highlight = (curr_highlight + 1) % 4;
      Serial.println("A short press is detected");
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      Serial.println("A long press is detected");
      tft.fillScreen(TFT_BLACK);
      roomScreen = false;
      teamScreen = true;
      lastRightState = currentRightState;
      return;
    }
  }
  lastRightState = currentRightState;

  // Room Screen UI
  for (int i = 0; i < 4; i++) {
    if (i == curr_highlight) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
    }
    tft.drawChar(char(room[i] + '0'), 30 + i * 20, tft.height()/2, 2);
  }
  tft.setTextColor(TFT_GREEN, TFT_BLACK);

}

void loop() {

  if (nameScreen) {
    handleNameEntry();
  }
  else if (roomScreen) {
    drawRoom();
  }
  else if (teamScreen) {

  }
  else if (gameScreen) {
    if (drawControlsOut){
      drawControls();
      drawControlsOut = false;
    }
    if (scheduleCmd1Send) {
      broadcast("D: " + cmd1);
      scheduleCmd1Send = false;
    }
    if (scheduleCmd2Send) {
      broadcast("D: " + cmd2);
      scheduleCmd2Send = false;
    }
    if (scheduleCmdAsk) {
      String cmdAsk = random(2) ? cmd1 : cmd2;
      broadcast("A: " + cmdAsk);
      scheduleCmdAsk = false;
    }
    if (askExpired) {
      progress = max(0, progress - 1);
      broadcast(String(progress));
      //tft.fillRect(0, 0, 135, 90, TFT_RED);
      cmdRecvd = waitingCmd;
      redrawCmdRecvd = true;
      askExpired = false;
    }

    if ((millis() - lastRedrawTime) > 50) {
      tft.fillRect(15, lineHeight * 2 + 14, 100, 6, TFT_GREEN);
      tft.fillRect(16, lineHeight * 2 + 14 + 1, (((expireLength * 1000000.0) - timerRead(askExpireTimer)) / (expireLength * 1000000.0)) * 98, 4, TFT_RED);
      lastRedrawTime = millis();
    }

    if (redrawCmdRecvd || redrawProgress) {
      tft.fillRect(0, 0, 135, 90, TFT_BLACK);
      tft.drawString(cmdRecvd.substring(0, cmdRecvd.indexOf(' ')), 0, 0, 2);
      tft.drawString(cmdRecvd.substring(cmdRecvd.indexOf(' ') + 1), 0, 0 + lineHeight, 2);
      redrawCmdRecvd = false;

      if (progress >= 100) {
        tft.fillScreen(TFT_BLUE);
        tft.setTextSize(3);
        tft.setTextColor(TFT_WHITE, TFT_BLUE);
        tft.drawString("GO", 45, 20, 2);
        tft.drawString("COMS", 20, 80, 2);
        tft.drawString("3930!", 18, 130, 2);
        delay(6000);
        ESP.restart();
      } else {
        tft.fillRect(15, lineHeight * 2 + 5, 100, 6, TFT_GREEN);
        tft.fillRect(16, lineHeight * 2 + 5 + 1, progress, 4, TFT_BLUE);
      }
      redrawProgress = false;
    }
  }
  else if (endScreen) {

  }
}