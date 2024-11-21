-# ESP32-SpaceTeam
Creative Embedded System Module 3 Project: This project uses the TFT_eSPI, WiFi.h, esp_now.h, and SPI.h libraries with the Lilygo TTGO ESP32 and Arduino IDE to play a 2 vs. 2 SpaceTeam game with your friends.

## Demo
[![Creative Embedded Module Project 3](http://img.youtube.com/vi/E0GkIxMT4ik/0.jpg)](https://youtu.be/E0GkIxMT4ik)

## Blog Post

You can take an in depth look at our process making this program in this following blog posts
- [Mahdi's Blog Post](https://chambray-dragon-de5.notion.site/2v2-ESP32-Spaceteam-144c917d299b8060b59de96814556e25?pvs=73)
- [Christine's Blog Post](https://gusty-tail-7d3.notion.site/SpaceTeam-1445621b054b804f8a08f787a8b950fb?pvs=74)
- [Enrista's Blog Post](https://flashy-tellurium-248.notion.site/Module-3-Distributed-Systems-14469c37328680c6b641c40c72db47d1?pvs=73)
- [Jie's Blog Post](https://medium.com/@jj3291/2-vs-2-spaceteam-game-fe7c9134ec4b)

## Table of Contents

- [Features and Tools](#features-and-tools)
- [How to Build the Project](#how-to-build-the-project)
- [Concept Design](#concept-design)
- [Contributors](#contributors)

## Features and Tools

- [Arduino IDE](https://support.arduino.cc/hc/en-us/articles/360019833020-Download-and-install-Arduino-IDE)
- [Lilygo TTGO ESP32](https://www.amazon.com/LILYGO-T-Display-Arduino-Development-CH9102F/dp/B099MPFJ9M?th=1)
- USB-C cable
- Three friends (you'll need four players to start the game!)

## How to Build the Project 

### 1. Clone the repository for each player:

   ```bash
   git clone https://github.com/asder8215/ESP32-SpaceTeam.git
   ```
### 2. Move the `espaceteam.ino` File
    
From the espaceteam folder, import or move the `espaceteam.ino` file into your Arduino Folder.
If Arduino IDE is not installed, look at this [Arduino Support Page](https://support.arduino.cc/hc/en-us/articles/360019833020-Download-and-install-Arduino-IDE) on how to install the Arduino IDE.

### 3. Set Up the Required Libraries

Follow these [installation steps](https://coms3930.notion.site/Lab-1-TFT-Display-a53b9c10137a4d95b22d301ec6009a94) to correctly set up the libraries needed to write and run code for TTGO ESP32.

### 4. Upload the Code
Once your Arduino IDE is set up, you can connect your ESP32 to your laptop or computer via USB-C and click on the `Upload` button on the top left of the Arduino IDE. This will make the code compile and store onto the ESP32.

### 5. After Uploading the Code

Once the code is uploaded, the ESP32 will display prompts asking you to use the **left** and **right** buttons to choose your name, room, and team. This process allows four players to be divided into two teams to play against each other.

- **Name Selection Screen**:
  - **Short press (left button)**: Scroll through letters A-Z.
  - **Short press (right button)**: Confirm the current character and move to the next character.
  - **Long press (right button)**: Confirm the name (three letters long).

- **Room Selection Screen**:
  - **Short press (left button)**: Scroll through numbers 0-9.
  - **Long press (left button)**: Move back to the name screen.
  - **Short press (right button)**: Confirm the current digit and move to the next digit.
  - **Long press (right button)**: Confirm the room number (four digits long).  

    ⚠️ Ensure all players enter the **same room number** to join the same game.

- **Team Selection Screen**:
  - **Short press (left button)**: Toggle between **Team A** and **Team B**.
  - **Long press (left button)**: Exit the team screen and return to the room selection screen.
  - **Short press (right button)**: Toggle the readiness status of the local player.
  - **Long press (right button)**: Attempt to start the game (only if conditions are met).  

    ⚠️ The game will only start when **both teams have at least two players** and **all players are marked as ready**.

### 6. Start Playing the Game

Each player will see one **top command** and two **button commands** displayed on their ESP32 screen. Players must shout out their top command, prompting their partner to press the corresponding left or right button if the command matches one of their button commands. The goal is for each team to collaborate internally while competing against the opposing team to reach a score of 10 (each correct command adds +1 to the respective team's score).

- **Left Short Press**: Select the player’s **command 1** option and notify the other ESP32 device that this command was chosen.  
- **Right Short Press**: Select the player’s **command 2** option and notify the other ESP32 device that this command was chosen.

### 7. End of the Game

The game concludes when one team reaches 10 points first. All players are notified of the winning team.

- **Rematch**: Press the **left button** to re-enter the team selection process and set up a new game.  
- **Exit**: Press the **right button** to leave the current game session and return to the name selection screen.

If you are unsure of how to play the game or want a quick guide, refer to the **demo above** to see how it works!

## Concept Design

These contain images of our SpaceTeam concept design. Some of these images may not be implemented or changed on how it works/shown in the game as it is currently.

- Name, Room, and Team Screens
![Initial Screen](images/initial_screen.jpg)
- Game Screen
![Game Screen](images/game_screen.jpg)
- End Screen
![End Screens](images/end_screen.jpg)

## Contributors

- Mahdi Ali-Raihan
- Enrista Ilo
- Christine Lam
- Jie Ji
