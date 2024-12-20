/*
  Spacetime using ESP32

  Modified for dynamic player management via ESP-NOW
  Originally created by Mark Santolucito for Barnard COMS 3930
  Based on DroneBot Workshop 2022 ESP-NOW Multi Unit Demo
*/

// Include Libraries
#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

SemaphoreHandle_t mutex;  // Declare a mutex handle

// Constants
#define SHORT_PRESS_TIME 500  // 500 milliseconds
#define LONG_PRESS_TIME  3000 // 3000 milliseconds
#define BUTTON_LEFT 0
#define BUTTON_RIGHT 35

// Player structure
#define MAX_NAME_LENGTH 4  // Including null terminator
struct Player {
  char name[MAX_NAME_LENGTH];
  uint8_t macAddr[6]; // Unique identifier
  int team; // 0 or 1
  bool ready;
};

// Room number
int localRoomNumber = 0;  // Initialize with default room number
uint8_t zeroArr[6] = {0}; // null array for comparison purposes

#define MAX_PLAYERS 4
// Zero-initialize the array
// Done this way instead of {} to be able to test
// placeholder player information for teamScreen handling
Player players[MAX_PLAYERS] = {
                              {{}, {}, 0, 0}, 
                              {{}, {}, 0, 0}, 
                              {{'B', 'O', 'B', '\0'}, {1, 2, 3, 4, 5, 6}, 1, 1},
                              {{'S', 'O', 'B', '\0'}, {7, 8, 9, 10, 11, 12}, 1, 1}
                              };

// Player players[MAX_PLAYERS] = {
//                               {{}, {}, 0, 0}, 
//                               {{}, {}, 0, 0}, 
//                               {{'B', 'O', 'B', '\0'}, {1, 2, 3, 4, 5, 6}, 1, 1},
//                               {{'S', 'O', 'B', '\0'}, {7, 8, 9, 10, 11, 12}, 1, 1}
//                               };

// players[1].name = {'B', 'O', 'B'};
// players[1].macAddr = {1, 2, 3, 4, 5, 6};
// players[1].team = 0;
// players[1].ready = 0;

int numPlayers = 1; // Start with 1 because of localPlayer
// int numPlayers = 3; // Start with 1 because of localPlayer

// Local player information
Player localPlayer;

// Screen states
enum ScreenState {
  NAME_SCREEN,
  ROOM_SCREEN,
  TEAM_SCREEN,
  GAME_SCREEN,
  END_SCREEN
};

enum WinState {
  TEAM_NONE,
  TEAM_A,
  TEAM_B
};

ScreenState currentScreen = NAME_SCREEN;
WinState endScreenState = TEAM_NONE;

// Name Entry Variables
char selectedLetters[3] = {'A', 'A', 'A'}; // Array to store the selected letters
int currentLetterIndex = 0;         

// Room Selection Variables
int roomDigits[4] = {0, 0, 0, 0};
int curr_highlight = 0;

// Button states
int lastLeftState = LOW;  // the previous state from the input pin
int lastRightState = LOW;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

// Team Screen Variables
#define TEAM_A_COLOR TFT_BLUE
#define TEAM_B_COLOR TFT_PURPLE

// Game Screen Variables
String cmd1 = "";
String cmd2 = "";
volatile bool scheduleCmd1Send = false;
volatile bool scheduleCmd2Send = false;

String cmdRecvd = "";
const String waitingCmd = "Wait for cmds";
bool redrawCmdRecvd = false;

// For drawing progress bars
int progress = 0;
bool redrawProgress = true;
int lastRedrawTime = 0;

// Cmd scheduling and asking
volatile bool scheduleCmdAsk = true;
hw_timer_t *askRequestTimer = NULL;
volatile bool askExpired = false;
hw_timer_t *askExpireTimer = NULL;
int expireLength = 25;

// Command options
#define ARRAY_SIZE 10
const String commandVerbs[ARRAY_SIZE] = { "Buzz", "Engage", "Floop", "Bother", "Twist", "Jingle", "Jangle", "Yank", "Press", "Play" };
const String commandNounsFirst[ARRAY_SIZE] = { "foo", "dev", "bobby", "jaw", "tooty", "wu", "fizz", "rot", "tea", "bee" };
const String commandNounsSecond[ARRAY_SIZE] = { "bars", "ices", "pins", "nobs", "zops", "tangs", "bells", "wels", "pops", "bops" };

int lineHeight = 30;

// Function prototypes

// Scheduler functions using hardware timer interrupts
void IRAM_ATTR sendCmd1();
void IRAM_ATTR sendCmd2();
void IRAM_ATTR onAskReqTimer();
void IRAM_ATTR onAskExpireTimer();

// Random command to display for Player's Game Screen
String genCommand();

// Local Player Related Functions
void updateLocalPlayerInPlayersArray();
void clearPlayersArray();

// ESP NOW Wifi Related Functions
void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength);
void sendJoinRequest();
void sendUpdateRequest();
void sendLeaveRequest();
void sendStartRequest();
void sendAskRequest();
void sendDecideRequest();
void sendProgressRequest();
void sendWinRequest();
void sendFullPlayerList(const uint8_t *recipientMac);
void sendPlayerInfo();
void broadcast(const String &message);
void receiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int dataLen);
void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status);

// Setup Related Functions
void espnowSetup();
void buttonSetup();
void textSetup();
void timerSetup();
void setup();

// Draw & Control Handle Related Functions
// For Screen
void drawNameEntryScreen();
void handleNameEntry();
void drawRoomScreen();
void handleRoomEntry();
void drawTeamScreen();
void handleTeamScreen();
// void drawGameScreen();
void handleGameScreen();
void drawControls();
void drawWinScreen();
void handleWinScreen();

// Main Loop Function
void loop();


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


void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength) {
  snprintf(buffer, maxLength, "%02X%02X%02X%02X%02X%02X",
           macAddr[0], macAddr[1], macAddr[2],
           macAddr[3], macAddr[4], macAddr[5]);
  return;
}

void sendJoinRequest() {
  // Prepare the JOIN_REQUEST message
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  char message[100];
  snprintf(message, sizeof(message), "JOIN_REQUEST:%d:%s:%s:%d:%d",
           localRoomNumber, macStr, localPlayer.name, localPlayer.team, localPlayer.ready);
  broadcast(String(message));
  return;
}

void sendUpdateRequest() {
  // Prepare the UPDATE message
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  char message[100];
  snprintf(message, sizeof(message), "UPDATE:%d:%s:%s:%d:%d",
           localRoomNumber, macStr, localPlayer.name, localPlayer.team, localPlayer.ready);
  broadcast(String(message));
  return;
}

void sendStartRequest() {
  // Prepare the JOIN_REQUEST message
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  char message[100];
  snprintf(message, sizeof(message), "START:%d:%s:%s:%d:%d",
           localRoomNumber, macStr, localPlayer.name, localPlayer.team, localPlayer.ready);
  broadcast(String(message));
  return;
}

void sendLeaveRequest() {
  // Prepare the LEAVE message
  detachInterrupt(digitalPinToInterrupt(BUTTON_LEFT));
  detachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT));
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  char message[100];
  snprintf(message, sizeof(message), "LEAVE:%d:%s:%s:%d:%d",
           localRoomNumber, macStr, localPlayer.name, localPlayer.team, localPlayer.ready);
  broadcast(String(message));

  // Deinitialize ESP-NOW
  if (esp_now_deinit() == ESP_OK) {
    Serial.println("ESP-NOW Deinit Success");
  } else {
    Serial.println("ESP-NOW Deinit Failed");
    delay(3000);
    ESP.restart();
  }
  return;
}

void sendAskRequest(const String &ask) {
  // Prepare the ASK request
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  String message = "ASK:" + String(localRoomNumber) + ":" + String(macStr) + ":" + String(localPlayer.name) + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready) + ":" + ask;
  Serial.println("Sending ASK message: " + message); // Debugging
  broadcast(message);
}

void sendDecideRequest(const String &decide) {
  // Prepare the DECIDE request
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  String message = "DECIDE:" + String(localRoomNumber) + ":" + String(macStr) + ":" + String(localPlayer.name) + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready) + ":" + decide;
  Serial.println("Sending DECIDE message: " + message); // Debugging
  broadcast(message);
}

void sendProgressRequest(const String &progress) {
  // Prepare the PROGRESS request
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  String message = "PROGRESS:" + String(localRoomNumber) + ":" + String(macStr) + ":" + String(localPlayer.name) + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready) + ":" + progress;
  Serial.println("Sending PROGRESS message: " + message); // Debugging
  broadcast(message);
}

void sendWinRequest() {
  detachInterrupt(digitalPinToInterrupt(BUTTON_LEFT));
  detachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT));
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  char message[100];
  snprintf(message, sizeof(message), "WIN:%d:%s:%s:%d:%d",
           localRoomNumber, macStr, localPlayer.name, localPlayer.team, localPlayer.ready);
  broadcast(String(message));
}

void sendFullPlayerList(const uint8_t *recipientMac) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    // send player list not including the recipient & empty players
    if (memcmp(players[i].macAddr, zeroArr, 6) != 0) {
      char macStr[13];
      formatMacAddress(players[i].macAddr, macStr, 13);
      char message[100];
      snprintf(message, sizeof(message), "UPDATE:%d:%s:%s:%d:%d",
              localRoomNumber, macStr, players[i].name, players[i].team, players[i].ready);
      esp_now_send(recipientMac, (const uint8_t *)message, strlen(message));
      delay(150); // add small delay between sending each msg
    }
  }
  return;
}

// void sendPlayerInfo() {
//   // Prepare the JOIN message
//   char macStr[13];
//   formatMacAddress(localPlayer.macAddr, macStr, 13);
//   String message = "JOIN:" + String(macStr) + ":" + localPlayer.name + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready);
//   broadcast(message);
// }

void updateLocalPlayerInPlayersArray() {
  // Since localPlayer is always at index 0
  players[0] = localPlayer;
}

void clearPlayersArray() {
  // Clear all players except localPlayer
  numPlayers = 1;
  for (int i = numPlayers; i < MAX_PLAYERS; i++) {
    memset(&players[i], 0, sizeof(Player));
  }
}

void broadcast(const String &message) {
  // Check message length
  if (message.length() > 250) {
    Serial.println("Error: Message length exceeds ESP-NOW maximum limit.");
    return;
  }
  // Broadcast a message to every device in range
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo);
  }
  // Send message
  esp_err_t result = esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());
  if (result == ESP_OK) {
    Serial.println("Broadcast message sent successfully.");
  } else {
    Serial.println("Error sending broadcast message.");
  }
  delay(150);
}



void receiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int dataLen) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {  // Take the mutex
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
    strncpy(buffer, (const char *)data, msgLen);
    buffer[msgLen] = 0;
    String recvd = String(buffer);
    Serial.println("Received message: " + recvd);

    // Parse the message
    int idx1 = recvd.indexOf(':');
    if (idx1 == -1) {
      xSemaphoreGive(mutex);
      delay(100);
      return;
    }
    String messageType = recvd.substring(0, idx1);
    recvd.remove(0, idx1 + 1);

    // Extract room number
    idx1 = recvd.indexOf(':');
    if (idx1 == -1) {
      xSemaphoreGive(mutex);
      delay(100);
      return;
    }
    String roomStr = recvd.substring(0, idx1);
    int messageRoomNumber = roomStr.toInt();
    if (messageRoomNumber != localRoomNumber) {
      // Ignore messages from other rooms
      xSemaphoreGive(mutex);
      delay(100);
      return;
    }
    recvd.remove(0, idx1 + 1);

    // Parse macStr
    idx1 = recvd.indexOf(':');
    if (idx1 == -1) {
      xSemaphoreGive(mutex);
      delay(100);
      return;
    }
    String macStr = recvd.substring(0, idx1);
    recvd.remove(0, idx1 + 1);

    // Parse nameStr
    idx1 = recvd.indexOf(':');
    if (idx1 == -1) {
      xSemaphoreGive(mutex);
      delay(100);
      return;
    }
    String nameStr = recvd.substring(0, idx1);
    recvd.remove(0, idx1 + 1);

    // Parse team
    idx1 = recvd.indexOf(':');
    if (idx1 == -1) {
      xSemaphoreGive(mutex);
      delay(100);
      return;
    }
    String teamStr = recvd.substring(0, idx1);
    int team = teamStr.toInt();
    recvd.remove(0, idx1 + 1);

    // Parse ready
    idx1 = recvd.indexOf(':');
    String readyStr;
    if (idx1 == -1) {
      // No more colons, so ready is the rest of the string
      readyStr = recvd;
      recvd = ""; // recvd is empty now
    } else {
      readyStr = recvd.substring(0, idx1);
      recvd.remove(0, idx1 + 1);
    }
    bool ready = readyStr.toInt();

    // Convert MAC string back to uint8_t[6]
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
      char byteStr[3];
      byteStr[0] = macStr[i * 2];
      byteStr[1] = macStr[i * 2 + 1];
      byteStr[2] = '\0';
      mac[i] = (uint8_t) strtol(byteStr, NULL, 16);
    }

    // Now handle the message based on messageType
    if (messageType == "JOIN_REQUEST") {

      // Check if player already exists
      bool playerExists = false;
      for (int i = 1; i < MAX_PLAYERS; i++) {
        if (memcmp(players[i].macAddr, mac, 6) == 0) {
          playerExists = true;
          break;
        }
      }

      bool newPlayerJoined = false;
      if (!playerExists && numPlayers < MAX_PLAYERS) {
        // Add new player at the end of the array
        int selectTeamSlot = numPlayers;
        while(true){
          if (memcmp(players[selectTeamSlot].macAddr, zeroArr, 6) == 0) {
            strncpy(players[selectTeamSlot].name, nameStr.c_str(), MAX_NAME_LENGTH - 1);
            players[selectTeamSlot].name[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination
            memcpy(players[selectTeamSlot].macAddr, mac, 6);
            players[selectTeamSlot].team = team;
            players[selectTeamSlot].ready = ready;
            numPlayers++;
            // Serial.println("Added new player!");
            newPlayerJoined = true;
            break;
          }
          selectTeamSlot = (selectTeamSlot + 1) % (MAX_PLAYERS - 1) + 1; // +1 to skip index 0
        }
      }


      // Send our player info to the new player, if they
      // are accepted in the room
      if (newPlayerJoined) {
        sendFullPlayerList(info->src_addr);
        // sendPlayerInfo();
      }

      // Redraw team screen if necessary
      if (currentScreen == TEAM_SCREEN) {
        drawTeamScreen();
      }

    } else if (messageType == "UPDATE") {
      // Check if player already exists
      bool playerExists = false;
      for (int i = 1; i < MAX_PLAYERS; i++) {
        if (memcmp(players[i].macAddr, mac, 6) == 0) {
          // Update player info
          strncpy(players[i].name, nameStr.c_str(), MAX_NAME_LENGTH - 1);
          players[i].name[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination
          players[i].team = team;
          players[i].ready = ready;
          playerExists = true;
          break;
        }
      }

      if (!playerExists && numPlayers < MAX_PLAYERS) {
        // Add new player at the end of the array
        int selectTeamSlot = numPlayers;
        while(true){
          if (memcmp(players[selectTeamSlot].macAddr, zeroArr, 6) == 0) {
            strncpy(players[selectTeamSlot].name, nameStr.c_str(), MAX_NAME_LENGTH - 1);
            players[selectTeamSlot].name[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination
            memcpy(players[selectTeamSlot].macAddr, mac, 6);
            players[selectTeamSlot].team = team;
            players[selectTeamSlot].ready = ready;
            numPlayers++;
            break;
          }
          selectTeamSlot = (selectTeamSlot + 1) % (MAX_PLAYERS - 1) + 1; // +1 to skip index 0
        }
      }

          // Redraw team screen if necessary
      if (currentScreen == TEAM_SCREEN) {
        drawTeamScreen();
      }

    } else if (messageType == "LEAVE") {

      // Remove player from array
      for (int i = 1; i < MAX_PLAYERS; i++) {
        if (memcmp(players[i].macAddr, mac, 6) == 0) {
          // players[i] = players[numPlayers - 1]; // Move last player to this spot
          memset(&players[i], 0, sizeof(Player));
          numPlayers--;
          break;
        }
      }

      // Redraw team screen if necessary
      if (currentScreen == TEAM_SCREEN) {
        drawTeamScreen();
      }

    } else if (messageType == "START") {
      // Move to game screen
      tft.fillScreen(TFT_BLACK);
      drawControls();
      attachInterrupt(digitalPinToInterrupt(BUTTON_LEFT), sendCmd1, FALLING);
      attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), sendCmd2, FALLING);
      timerSetup();
      currentScreen = GAME_SCREEN;

    } else if (messageType == "ASK") {
      if (cmdRecvd == waitingCmd && random(100) < 30 && team == localPlayer.team) {
        String ask = recvd; // recvd contains the ask parameter
        // Corrected character validation
        for (char c : ask) {
          if (!( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' )) {
            Serial.println("Message contains invalid characters!");
            xSemaphoreGive(mutex);
            delay(100);
            return;
          }
        }
        cmdRecvd = ask;
        redrawCmdRecvd = true;
        timerStart(askExpireTimer);  //once you get an ask, a timer starts
      }


    } else if (messageType == "DECIDE") {
      if (team == localPlayer.team) {
        String decide = recvd; // recvd contains the decide parameter
        // Corrected character validation
        for (char c : decide) {
          if (!( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' )) {
            Serial.println("Message contains invalid characters!");
            xSemaphoreGive(mutex);
            delay(100);
            return;
          }
        }
        if (decide == cmdRecvd) {
          timerWrite(askExpireTimer, 0);
          timerStop(askExpireTimer);
          cmdRecvd = waitingCmd;
          progress = progress + 1;
          sendProgressRequest(String(progress));
          redrawCmdRecvd = true;
        }
      }
    } else if (messageType == "PROGRESS") {
      String progressTxt = recvd; // recvd contains the progress parameter
      // Check for basic corruption (unprintable characters)
      for (char c : progressTxt) {
        if (!(c >= '0' && c <= '9')) {
          Serial.println("Message contains invalid characters!");
          xSemaphoreGive(mutex);
          delay(100);
          return;
        }
      }
      if (team == localPlayer.team) {
        progress = progressTxt.toInt();
        redrawProgress = true;
      }
} else if (messageType == "WIN") {
      currentScreen = END_SCREEN;
      tft.fillScreen(TFT_BLACK);
      if (team == 0) {
        endScreenState = TEAM_A;
      } else {
        endScreenState = TEAM_B;
      }
    }


    xSemaphoreGive(mutex);
  }
  delay(100);
  return;
}


void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status) {
  // Optional: Handle send status
  return;
}

void espnowSetup() {
  // Set ESP32 in STA mode
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(500);
  Serial.println("ESP-NOW Broadcast Demo");

  // Get local MAC address
  WiFi.macAddress(localPlayer.macAddr);
  // Serial.print("Local Player Mac Address: ");
  // Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
  //               localPlayer.macAddr[0], 
  //               localPlayer.macAddr[1], 
  //               localPlayer.macAddr[2],
  //               localPlayer.macAddr[3], 
  //               localPlayer.macAddr[4], 
  //               localPlayer.macAddr[5]);

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
}

void textSetup() {
  tft.init();
  tft.setRotation(0);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
}

void timerSetup() {
  // https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/api/timer.html
  askRequestTimer = timerBegin(1000000); // 1MHz
  timerAttachInterrupt(askRequestTimer, &onAskReqTimer);
  timerAlarm(askRequestTimer, 5 * 1000000, true, 0);  //send out an ask every 5 secs

  askExpireTimer = timerBegin(1000000);
  timerAttachInterrupt(askExpireTimer, &onAskExpireTimer);
  timerAlarm(askExpireTimer, expireLength * 1000000, true, 0);
  timerStop(askExpireTimer);
}

void setup() {
  Serial.begin(115200);
  delay(1000); // to let esp32 connect properly
  textSetup();
  buttonSetup();
  // Initialize localPlayer
  memset(&localPlayer, 0, sizeof(Player));

  mutex = xSemaphoreCreateMutex();

  if (mutex == NULL) {
    Serial.println("Mutex creation failed!");
  }

  // localPlayer will be added to players[0] in updateLocalPlayerInPlayersArray()
  return;
}

void drawNameEntryScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Enter", tft.width()/4+7, 30, 2);
  tft.drawString("Name", tft.width()/4+7, 70, 2);

  for (int i = 0; i < 3; i++) {
    if (i == currentLetterIndex) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
    }
    tft.drawChar(selectedLetters[i], 35 + i * 30, 130, 2);
  }
  return;
}

void handleNameEntry() {
  static int lastLetterIndex = -1;
  static char lastLetters[3] = {'\0', '\0', '\0'};

  int currentLeftState = digitalRead(BUTTON_LEFT);

  if (lastLeftState == HIGH && currentLeftState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
        selectedLetters[currentLetterIndex]++;
        if (selectedLetters[currentLetterIndex] > 'Z') {
            selectedLetters[currentLetterIndex] = 'A';
        }
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
        currentLetterIndex = (currentLetterIndex + 1) % 3;
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      // Finalize name and proceed to room selection
      for (int i = 0; i < 3; i++) {
        localPlayer.name[i] = selectedLetters[i];
      }
      localPlayer.name[3] = '\0'; // Null-terminate
      localPlayer.team = 0; // Default team
      localPlayer.ready = false;
      currentScreen = ROOM_SCREEN;
      tft.fillScreen(TFT_BLACK);
      lastRightState = currentRightState; 
      return;
    }
  }
  lastRightState = currentRightState;   

  // Redraw only if changes occurred
  if (currentLetterIndex != lastLetterIndex || memcmp(selectedLetters, lastLetters, 3) != 0) {
    drawNameEntryScreen();
    lastLetterIndex = currentLetterIndex;
    memcpy(lastLetters, selectedLetters, 3);
  }
  return;
}

void drawRoomScreen() {
  // tft.fillScreen(TFT_BLACK);
  tft.drawString("Room Num", tft.width()/4 - 20, 30, 2);

  // Room Screen UI
  for (int i = 0; i < 4; i++) {
    if (i == curr_highlight) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
    }
    tft.drawChar(char(roomDigits[i] + '0'), 30 + i * 20, tft.height()/2, 2);
  }
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  return;
}

void handleRoomEntry() {
  int currentLeftState = digitalRead(BUTTON_LEFT);

  // Detect left button press
  if (lastLeftState == HIGH && currentLeftState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      roomDigits[curr_highlight] = (roomDigits[curr_highlight] + 1) % 10;
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      tft.fillScreen(TFT_BLACK);
      currentScreen = NAME_SCREEN;
      lastLeftState = currentLeftState;
      return;
    }
  }

  lastLeftState = currentLeftState;

  int currentRightState = digitalRead(BUTTON_RIGHT);

  // Detect right button press
  if (lastRightState == HIGH && currentRightState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastRightState == LOW && currentRightState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      curr_highlight = (curr_highlight + 1) % 4;
      // tft.fillScreen(TFT_BLACK);
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      // Set local room number
      localRoomNumber = roomDigits[0]*1000 + roomDigits[1]*100 + roomDigits[2]*10 + roomDigits[3];
      tft.fillScreen(TFT_BLACK);

      currentScreen = TEAM_SCREEN;
      espnowSetup();

      // **Add local player to players array**
      updateLocalPlayerInPlayersArray();

      // Send initial join request
      sendJoinRequest();
      lastRightState = currentRightState;
      drawTeamScreen();
      return;
    }
  }
  lastRightState = currentRightState;
  drawRoomScreen();
  return;
}

void drawTeamScreen() {
  tft.fillScreen(TFT_BLACK);

  // Draw Team A header
  tft.setTextColor(TEAM_A_COLOR, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Team A", 20, 20);

  // Draw Team B header
  tft.setTextColor(TEAM_B_COLOR, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Team B", 20, 120);

  // Draw players
  int teamAPosition = 50; // Vertical position for Team A names
  int teamBPosition = 150; // Vertical position for Team B names
  for (int i = 0; i < MAX_PLAYERS; i++) {
    // if looking at null array, move to the next one
    if (memcmp(players[i].macAddr, zeroArr, 6) == 0) {
      continue;
    }

    if (memcmp(players[i].macAddr, localPlayer.macAddr, 6) == 0) {
      // Highlight the local player
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      tft.setTextColor(players[i].team == 0 ? TEAM_A_COLOR : TEAM_B_COLOR, TFT_BLACK);
    }

    int positionY = players[i].team == 0 ? teamAPosition : teamBPosition;
    tft.drawString(players[i].name, 30, positionY);

    // Draw readiness indicator after the name
    if (players[i].ready) {
      int nameWidth = tft.textWidth(players[i].name, 2); // Calculate the width of the name
      tft.drawString(".", 30 + nameWidth + 5, positionY);
    }

    if (players[i].team == 0) {
      teamAPosition += 20;
    } else {
      teamBPosition += 20;
    }
  }
  return;
}

void handleTeamScreen() {
  static unsigned long pressedTimeLeft = 0;
  static unsigned long releasedTimeLeft = 0;
  static unsigned long pressedTimeRight = 0;
  static unsigned long releasedTimeRight = 0;

  int currentLeftState = digitalRead(BUTTON_LEFT);
  int currentRightState = digitalRead(BUTTON_RIGHT);

  // Handle left button (toggle team assignment)
  if (lastLeftState == HIGH && currentLeftState == LOW) { // button is pressed
    pressedTimeLeft = millis();
  } else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTimeLeft = millis();
    long pressDuration = releasedTimeLeft - pressedTimeLeft;

    if (pressDuration < SHORT_PRESS_TIME) {
      // Toggle the team for the local player
      localPlayer.team = 1 - localPlayer.team;

      // Update local player in players array
      updateLocalPlayerInPlayersArray();

      sendUpdateRequest(); // Broadcast the change
      drawTeamScreen(); // Redraw the screen with updated teams
    } else if (pressDuration > LONG_PRESS_TIME) {
      currentScreen = ROOM_SCREEN;
      tft.fillScreen(TFT_BLACK);
      sendLeaveRequest();
      clearPlayersArray();
      lastLeftState = currentLeftState;
    }
  }
  lastLeftState = currentLeftState;

  // Handle right button (toggle readiness or proceed to game screen)
  if (lastRightState == HIGH && currentRightState == LOW) { // button is pressed
    pressedTimeRight = millis();
  } else if (lastRightState == LOW && currentRightState == HIGH) { // button is released
    releasedTimeRight = millis();
    long pressDuration = releasedTimeRight - pressedTimeRight;

    if (pressDuration < SHORT_PRESS_TIME) {
      // Toggle readiness for the local player
      localPlayer.ready = !localPlayer.ready; // Toggle readiness

      // Update local player in players array
      updateLocalPlayerInPlayersArray();

      sendUpdateRequest(); // Broadcast the change
      drawTeamScreen(); // Redraw the screen with updated readiness
    } else if (pressDuration > LONG_PRESS_TIME) {
      // Check if game can start
      int teamACount = 0;
      int teamBCount = 0;
      int teamAReady = 0;
      int teamBReady = 0;
      for (int i = 0; i < MAX_PLAYERS; i++) {
        if (memcmp(players[i].macAddr, zeroArr, 6) != 0) {
          if (players[i].team == 0) {
            teamACount++;
            if (players[i].ready) teamAReady++;
          } else {
            teamBCount++;
            if (players[i].ready) teamBReady++;
          }
        }
      }

      // Check if both teams have at least 1 ready player
      if (teamACount >= 2 && teamAReady >= 2 && teamBCount >= 2 && teamBReady >= 2) {
        // Proceed to game screen
        currentScreen = GAME_SCREEN;
        tft.fillScreen(TFT_BLACK);
        // Notify others to start the game
        // broadcast("START");
        drawControls();
        attachInterrupt(digitalPinToInterrupt(BUTTON_LEFT), sendCmd1, FALLING);
        attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), sendCmd2, FALLING);
        timerSetup();
        sendStartRequest();
      } else {
        // Display a message or indicate that the game cannot start
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Not enough players", 10, 200);
      }
    }
  }
  lastRightState = currentRightState;
  return;
}

String genCommand() {
  String verb = commandVerbs[random(ARRAY_SIZE)];
  String noun1 = commandNounsFirst[random(ARRAY_SIZE)];
  String noun2 = commandNounsSecond[random(ARRAY_SIZE)];
  return verb + " " + noun1 + noun2;
}

// Will rely on attachInterrupts for button inputs
// no need to do short press or long press
void handleGameScreen(){
  if (scheduleCmd1Send) {
    sendDecideRequest(cmd1);
    // broadcast("DECIDE:" + String(localPlayer.team) + ":" + cmd1);
    scheduleCmd1Send = false;
  }
  if (scheduleCmd2Send) {
    sendDecideRequest(cmd2);
    // broadcast("DECIDE:" + String(localPlayer.team) + ":" + cmd2);
    scheduleCmd2Send = false;
  }
  if (scheduleCmdAsk) {
    String cmdAsk = random(2) ? cmd1 : cmd2;
    // broadcast("ASK:" + String(localPlayer.team) + ":" + cmdAsk);
    sendAskRequest(cmdAsk);
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

    if (progress >= 10) {
      // tft.fillScreen(TFT_BLUE);
      tft.fillScreen(TFT_BLACK);
      currentScreen = END_SCREEN;
      sendWinRequest();
      drawWinScreen();
      detachInterrupt(digitalPinToInterrupt(BUTTON_LEFT));
      detachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT));
      // ESP.restart();
    } else {
      tft.fillRect(15, lineHeight * 2 + 5, 100, 6, TFT_GREEN);
      tft.fillRect(16, lineHeight * 2 + 5 + 1, progress, 4, TFT_BLUE);
    }
    redrawProgress = false;
  }
}

void drawControls() {
  cmd1 = genCommand();
  cmd2 = genCommand();
  cmd1.indexOf(' ');
  String currTeam = (localPlayer.team) ? "B" : "A";
  tft.drawString(currTeam + "1: " + cmd1.substring(0, cmd1.indexOf(' ')), 0, 90, 2);
  tft.drawString(cmd1.substring(cmd1.indexOf(' ') + 1), 0, 90 + lineHeight, 2);
  tft.drawString(currTeam + "2: " + cmd2.substring(0, cmd2.indexOf(' ')), 0, 170, 2);
  tft.drawString(cmd2.substring(cmd2.indexOf(' ') + 1), 0, 170 + lineHeight, 2);
}

// void drawGameScreen() {
//   String teamWinsLine1 = "Lorem";
//   int16_t x1 = (tft.width() - tft.textWidth(teamWinsLine1, 2)) / 2; // Center horizontally
//   tft.drawString(teamWinsLine1, x1, 20, 2);

//   // Second part
//   String teamWinsLine2 = "Ipsum";
//   x1 = (tft.width() - tft.textWidth(teamWinsLine2, 2)) / 2;
//   tft.drawString(teamWinsLine2, x1, 80, 2);

//   tft.setTextSize(2); // Set a larger font for emphasis
//   String rematch = "Dolor";
//   x1 = (tft.width() - tft.textWidth(rematch, 2)) / 2; // Center horizontally
//   tft.drawString(rematch, x1, 140, 2);
// }

void handleWinScreen() {
  int currentLeftState = digitalRead(BUTTON_LEFT); // rematch
  int currentRightState = digitalRead(BUTTON_RIGHT); // quit

  if (currentLeftState == LOW) {
    // player wants to replay, return to gameScreen
    tft.fillScreen(TFT_BLACK);
    currentScreen = TEAM_SCREEN;
    // **Add local player to players array**
    updateLocalPlayerInPlayersArray();

    // Send initial player info
    sendUpdateRequest();
    drawTeamScreen();
    return;
  }
  else if (currentRightState == LOW) { 
    // player picked quit, returns other players to waiting room (teamScreen)
    tft.fillScreen(TFT_BLACK);
    currentScreen = NAME_SCREEN;
    sendLeaveRequest();
    clearPlayersArray();
    return;
  }
  drawWinScreen();
  return;
}

void drawWinScreen() {
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(3); // Set a larger font for emphasis

  String winningTeam = (endScreenState == TEAM_A) ? "A" : "B";

  String teamWinsLine1 = "Team " + winningTeam;
  int16_t x1 = (tft.width() - tft.textWidth(teamWinsLine1, 2)) / 2; // Center horizontally
  tft.drawString(teamWinsLine1, x1, 20, 2);

  // Second part
  String teamWinsLine2 = "WINS!";
  x1 = (tft.width() - tft.textWidth(teamWinsLine2, 2)) / 2;
  tft.drawString(teamWinsLine2, x1, 80, 2);

  tft.setTextSize(2); // Set a larger font for emphasis
  String rematch = "Rematch?";
  x1 = (tft.width() - tft.textWidth(rematch, 2)) / 2; // Center horizontally
  tft.drawString(rematch, x1, 140, 2);

  String quit = "Quit?";
  x1 = (tft.width() - tft.textWidth(quit, 2)) / 2; // Center horizontally
  tft.drawString(quit, x1, 170, 2);
  return;
}


void loop() {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {  // Take the mutex
    switch (currentScreen) {
      case NAME_SCREEN:
        handleNameEntry();
        break;
      case ROOM_SCREEN:
        handleRoomEntry();
        break;
      case TEAM_SCREEN:
        handleTeamScreen();
        break;
      case GAME_SCREEN:
        // Game logic here
        handleGameScreen();
        break;
      case END_SCREEN:
        handleWinScreen();
        break;
    }
    xSemaphoreGive(mutex);
  }
  delay(100);
}