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

// Constants
#define SHORT_PRESS_TIME 500 // 500 milliseconds
#define LONG_PRESS_TIME  3000 // 3000 milliseconds
#define BUTTON_LEFT 0
#define BUTTON_RIGHT 35

// Player structure
struct Player {
  String name;
  uint8_t macAddr[6]; // Unique identifier
  int team; // 0 or 1
  bool ready;
};

#define MAX_PLAYERS 10
// Player players[MAX_PLAYERS];
Player *players = (Player *) malloc(4 * sizeof(Player));
int numPlayers = 0;

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

ScreenState currentScreen = NAME_SCREEN;

// Name Entry Variables
String userName = "___";          
int currentLetterIndex = 0;         
char selectedLetters[3] = {'A', 'A', 'A'}; // Array to store the selected letters

// Room Selection Variables
int room[4] = {0, 0, 0, 0};
int curr_highlight = 0;

// Button states
int lastLeftState = LOW;  // the previous state from the input pin
int lastRightState = LOW;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

// Team Screen Variables
int selectedPlayerIndex = 0; // Index in players[]

#define TEAM_A_COLOR TFT_BLUE
#define TEAM_B_COLOR TFT_PURPLE

// Function prototypes
void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength);
void sendPlayerInfo();
void sendPlayerUpdate();
void receiveCallback(const esp_now_recv_info_t *macAddr, const uint8_t *data, int dataLen);
void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status);
void broadcast(const String &message);
void espnowSetup();
void buttonSetup();
void textSetup();
void drawNameEntryScreen();
void handleNameEntry();
void drawRoomScreen();
void handleRoomEntry();
void drawTeamScreen();
void handleTeamScreen();
void updateLocalPlayerInPlayersArray();
void setup();
void loop();

// Implementations
void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength) {
  snprintf(buffer, maxLength, "%02X%02X%02X%02X%02X%02X",
           macAddr[0], macAddr[1], macAddr[2],
           macAddr[3], macAddr[4], macAddr[5]);
}

void sendPlayerInfo() {
  // Prepare the JOIN message
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  String message = "JOIN:" + String(macStr) + ":" + localPlayer.name + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready);
  broadcast(message);
}

void sendPlayerUpdate() {
  // Prepare the UPDATE message
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  String message = "UPDATE:" + String(macStr) + ":" + localPlayer.name + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready);
  broadcast(message);
}

void sendLeaveUpdate() {
  // Prepare the LEAVE message
  char macStr[13];
  formatMacAddress(localPlayer.macAddr, macStr, 13);
  String message = "LEAVE:" + String(macStr) + ":" + localPlayer.name + ":" + String(localPlayer.team) + ":" + String(localPlayer.ready);
  broadcast(message);

}

void receiveCallback(const esp_now_recv_info_t *macAddr, const uint8_t *data, int dataLen) {
  // Only allow a maximum of 250 characters in the message + a null terminating byte
  char buffer[ESP_NOW_MAX_DATA_LEN + 1];
  int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
  strncpy(buffer, (const char *)data, msgLen);

  // Make sure we are null terminated
  buffer[msgLen] = 0;
  String recvd = String(buffer);
  Serial.println("Received message: " + recvd);

  // Parse the message
  if (recvd.startsWith("JOIN:") || recvd.startsWith("UPDATE:")) {
    bool isJoin = recvd.startsWith("JOIN:");
    recvd.remove(0, isJoin ? 5 : 7);
    int idx1 = recvd.indexOf(':');
    int idx2 = recvd.indexOf(':', idx1 + 1);
    int idx3 = recvd.indexOf(':', idx2 + 1);

    String macStr = recvd.substring(0, idx1);
    String name = recvd.substring(idx1 + 1, idx2);
    int team = recvd.substring(idx2 + 1, idx3).toInt();
    bool ready = recvd.substring(idx3 + 1).toInt();

    // Convert MAC string back to uint8_t[6]
    uint8_t mac[6];
    sscanf(macStr.c_str(), "%02X%02X%02X%02X%02X%02X",
           &mac[0], &mac[1], &mac[2],
           &mac[3], &mac[4], &mac[5]);

    // Check if player already exists
    bool playerExists = false;
    for (int i = 0; i < numPlayers; i++) {
      if (memcmp(players[i].macAddr, mac, 6) == 0) {
        // Update player info
        players[i].name = name;
        players[i].team = team;
        players[i].ready = ready;
        playerExists = true;
        break;
      }
    }

    if (!playerExists && numPlayers < MAX_PLAYERS) {
      // fits player into the first available spot in the players
      // list
      for (int i = 0; i < MAX_PLAYERS; i++) {
        if (memcmp(players[i].macAddr, NULL, 6) == 0) {
          // memcpy(players[numPlayers].macAddr, mac, 6);
          // players[numPlayers].name = name;
          // players[numPlayers].team = team;
          // players[numPlayers].ready = ready;
          memcpy(players[i].macAddr, mac, 6);
          players[i].name = name;
          players[i].team = team;
          players[i].ready = ready;
          numPlayers++;
          break;
        }
      }
    }

    // Redraw team screen if necessary
    if (currentScreen == TEAM_SCREEN) {
      drawTeamScreen();
    }

    // If received JOIN message, send back our player info
    if (isJoin) {
      sendPlayerInfo();
    }
  } else if (recvd.startsWith("LEAVE:")) {
    bool isLeave = recvd.startsWith("LEAVE:");
    recvd.remove(0, 6);
    int idx1 = recvd.indexOf(':');
    int idx2 = recvd.indexOf(':', idx1 + 1);
    int idx3 = recvd.indexOf(':', idx2 + 1);

    String macStr = recvd.substring(0, idx1);
    String name = recvd.substring(idx1 + 1, idx2);
    int team = recvd.substring(idx2 + 1, idx3).toInt();
    bool ready = recvd.substring(idx3 + 1).toInt();

    // Convert MAC string back to uint8_t[6]
    uint8_t mac[6];
    sscanf(macStr.c_str(), "%02X%02X%02X%02X%02X%02X",
           &mac[0], &mac[1], &mac[2],
           &mac[3], &mac[4], &mac[5]);

    
    // // Check if player already exists
    for (int i = 0; i < numPlayers; i++) {
      if (memcmp(players[i].macAddr, mac, 6) == 0) {
        // Player info cleared out
        memset(&players[i], 0, sizeof(players[i]));
        numPlayers--;
        break;
      }
    }
  } else if (recvd.startsWith("START")) {
    // Move to game screen
    currentScreen = GAME_SCREEN;
    tft.fillScreen(TFT_BLACK);
  }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status) {
  // Debug print
  // char macStr[18];
  // formatMacAddress(macAddr, macStr, 18);
  // Serial.print("Last Packet Sent to: ");
  // Serial.println(macStr);
  // Serial.print("Last Packet Send Status: ");
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void broadcast(const String &message) {
  // Broadcast a message to every device in range
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo);
  }
  // Send message
  esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());
}

void espnowSetup() {
  // Set ESP32 in STA mode
  delay(500);
  WiFi.mode(WIFI_STA);
  Serial.println("ESP-NOW Broadcast Demo");

  // Get local MAC address
  WiFi.macAddress(localPlayer.macAddr);

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

void setup() {
  Serial.begin(115200);
  textSetup();
  buttonSetup();
  espnowSetup();
  // drawNameEntryScreen(); 
}

void drawNameEntryScreen() {
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
        // if (currentLetterIndex < 2){
        //   currentLetterIndex++; // move to next digit
        // } else {
        //   currentLetterIndex = 0;
        // }
        currentLetterIndex = (currentLetterIndex + 1) % 3;
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      // Finalize name and proceed to room selection
      String newName = String(selectedLetters[0]) + String(selectedLetters[1]) + String(selectedLetters[2]);
      userName = newName;
      localPlayer.name = userName;
      localPlayer.team = 0; // Default team
      localPlayer.ready = false;
      currentScreen = ROOM_SCREEN;
      tft.fillScreen(TFT_BLACK);
      lastRightState = currentRightState; 
      return;
    }
  }
  lastRightState = currentRightState;   

  // update the userName and redraw if name or current letter index changes
  String newName = String(selectedLetters[0]) + String(selectedLetters[1]) + String(selectedLetters[2]);
  if (newName != lastName || currentLetterIndex != lastLetterIndex) {
      // drawNameEntryScreen();
      lastName = newName;
      lastLetterIndex = currentLetterIndex;
  }
  drawNameEntryScreen();
}

void drawRoomScreen() {
  tft.drawString("Room Num", tft.width()/4 - 20, 30, 2);

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

void handleRoomEntry() {
  int currentLeftState = digitalRead(BUTTON_LEFT);

  // Detect left button press
  if (lastLeftState == HIGH && currentLeftState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      room[curr_highlight] = (room[curr_highlight] + 1) % 10;
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
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      tft.fillScreen(TFT_BLACK);

      currentScreen = TEAM_SCREEN;

      // **Add local player to players array**
      updateLocalPlayerInPlayersArray();

      // Send initial player info
      sendPlayerInfo();
      lastRightState = currentRightState;
      drawTeamScreen();
      return;
    }
  }
  lastRightState = currentRightState;
  drawRoomScreen();
}

void updateLocalPlayerInPlayersArray() {
  bool playerExists = false;
  for (int i = 0; i < numPlayers; i++) {
    if (memcmp(players[i].macAddr, localPlayer.macAddr, 6) == 0) {
      // Update player info
      players[i].name = localPlayer.name;
      players[i].team = localPlayer.team;
      players[i].ready = localPlayer.ready;
      playerExists = true;
      break;
    }
  }
  if (!playerExists && numPlayers < MAX_PLAYERS) {
    // Add new player
    memcpy(players[numPlayers].macAddr, localPlayer.macAddr, 6);
    players[numPlayers].name = localPlayer.name;
    players[numPlayers].team = localPlayer.team;
    players[numPlayers].ready = localPlayer.ready;
    numPlayers++;
  }
}

void clearLocalPlayerInPlayersArray() {
  for (int i = 0; i < numPlayers; i++) {
    memset(&players[i], 0, sizeof(players[i]));
  }
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

  // Draw players in Team A
  int teamAPosition = 50; // Vertical position for Team A names
  int teamACount = 0;
  int teamBCount = 0;
  for (int i = 0; i < numPlayers; i++) {
    if (players[i].team == 0) { // Team A
      tft.setTextColor(TEAM_A_COLOR, TFT_BLACK);
      tft.drawString(players[i].name, 20, teamAPosition);

      // Draw readiness indicator after the name
      if (players[i].ready) {
        int nameWidth = tft.textWidth(players[i].name, 2); // Calculate the width of the name
        tft.drawString(".", 20 + nameWidth + 5, teamAPosition);
      }

      teamAPosition += 30; // Increment position for Team A
      teamACount++;
    }
  }

  // Draw players in Team B
  int teamBPosition = 150; // Vertical position for Team B names
  for (int i = 0; i < numPlayers; i++) {
    if (players[i].team == 1) { // Team B
      tft.setTextColor(TEAM_B_COLOR, TFT_BLACK);
      tft.drawString(players[i].name, 20, teamBPosition);

      // Draw readiness indicator after the name
      if (players[i].ready) {
        int nameWidth = tft.textWidth(players[i].name, 2); // Calculate the width of the name
        tft.drawString(".", 20 + nameWidth + 5, teamBPosition);
      }

      teamBPosition += 30; // Increment position for Team B
      teamBCount++;
    }
  }

  // Highlight the local player
  for (int i = 0; i < numPlayers; i++) {
    if (memcmp(players[i].macAddr, localPlayer.macAddr, 6) == 0) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      int position;
      if (players[i].team == 0) {
        position = 50 + 30 * i;
      } else {
        position = 150 + 30 * i;
      }
      tft.drawString(">", 5, position);
      break;
    }
  }
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

      // **Update local player in players array**
      updateLocalPlayerInPlayersArray();

      sendPlayerUpdate(); // Broadcast the change
      drawTeamScreen(); // Redraw the screen with updated teams
    } else if (pressDuration < LONG_PRESS_TIME) {
      currentScreen = ROOM_SCREEN;
      tft.fillScreen(TFT_BLACK);
      clearLocalPlayerInPlayersArray();
      sendLeaveUpdate();
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

      // **Update local player in players array**
      updateLocalPlayerInPlayersArray();

      sendPlayerUpdate(); // Broadcast the change
      drawTeamScreen(); // Redraw the screen with updated readiness
    } else if (pressDuration > LONG_PRESS_TIME) {
      // Check if game can start
      int teamACount = 0;
      int teamBCount = 0;
      int teamAReady = 0;
      int teamBReady = 0;
      for (int i = 0; i < numPlayers; i++) {
        if (players[i].team == 0) {
          teamACount++;
          if (players[i].ready) teamAReady++;
        } else {
          teamBCount++;
          if (players[i].ready) teamBReady++;
        }
      }

      // Check if both teams have at least 2 ready players
      if (teamACount >= 2 && teamAReady >= 2 && teamBCount >= 2 && teamBReady >= 2) {
        // Proceed to game screen
        currentScreen = GAME_SCREEN;
        tft.fillScreen(TFT_BLACK);
        // Notify others to start the game
        broadcast("START");
      } else {
        // Display a message or indicate that the game cannot start
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Not enough players", 10, 200);
      }
    }
  }
  lastRightState = currentRightState;
}

void handleWinScreen(){
  int currentLeftState = digitalRead(BUTTON_LEFT); // rematch
  int currentRightState = digitalRead(BUTTON_RIGHT); // quit

  if (currentLeftState == LOW) {
    // player wants to replay, return to gameScreen
    tft.fillScreen(TFT_BLACK);
    currentScreen = TEAM_SCREEN;
    // **Add local player to players array**
    updateLocalPlayerInPlayersArray();

    // Send initial player info
    sendPlayerInfo();
    drawTeamScreen();
    return;
  }
  else if (currentRightState == LOW) { 
    // player picked quit, returns other players to waiting room (teamScreen)
    tft.fillScreen(TFT_BLACK);
    currentScreen = NAME_SCREEN;
    // call drawteamScreen function?
    return;
  }
  drawWinScreen();
}

void drawWinScreen(){
  // variable for teamScreen to display winner name
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(3); // Set a larger font for emphasis

  String teamWinsLine1 = "Team _";
  int16_t x1 = (tft.width() - tft.textWidth(teamWinsLine1, 2)) / 2; // Center horizontally
  tft.drawString(teamWinsLine1, x1, 20, 2);

  // Second part
  String teamWinsLine2 = "WINS!";
  x1 = (tft.width() - tft.textWidth(teamWinsLine2, 2)) / 2;
  tft.drawString(teamWinsLine2, x1, 80, 2);
/*
  String teamWins = "Team _ Wins";
  int16_t x1 = (tft.width() - tft.textWidth(teamWins, 2)) / 2; // Center horizontally
  tft.drawString(teamWins, x1, 30, 2);*/

  tft.setTextSize(2); // Set a larger font for emphasis
  String rematch = "Rematch?";
  x1 = (tft.width() - tft.textWidth(rematch, 2)) / 2; // Center horizontally
  tft.drawString(rematch, x1, 140, 2);

  String quit = "Quit?";
  x1 = (tft.width() - tft.textWidth(quit, 2)) / 2; // Center horizontally
  tft.drawString(quit, x1, 170, 2);
  /*
  tft.drawString("Team _ Wins", tft.width()/4 - 20, 30, 2);
  tft.drawString("Rematch?", tft.width()/4 - 20, 60, 2);
  tft.drawString("Quit?", tft.width()/4 - 20, 90, 2); */

  // int currentLeftState = digitalRead(BUTTON_LEFT); // rematch
  // int currentRightState = digitalRead(BUTTON_RIGHT); // quit

  // if (currentLeftState == LOW) {
  //   // player wants to replay, return to gameScreen
  //   tft.fillScreen(TFT_BLACK);
  //   teamScreen = true;
  //   endScreen = false;
  //   // call drawGameScreen?
  // }
  // else if (currentRightState == LOW) { 
  //   // player picked quit, returns other players to waiting room (teamScreen)
  //   tft.fillScreen(TFT_BLACK);
  //   nameScreen = true;
  //   endScreen = false;
  //   // call drawteamScreen function?
  // }
}

void loop() {
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
      break;
    case END_SCREEN:
      handleWinScreen();
      // End screen logic here
      break;
  }
}