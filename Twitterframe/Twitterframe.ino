#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <WiFi101.h>
#include <Arduino_JSON.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "tjpgd.h"

// MUST UPDATE FIRMWARE FOR MKR1000 AND ADD api.twitter.com CERT

// Twitter related fields
String username = "";
String accountID = "";
String bearerToken = "";
String latestTweetID = "";

int lastRetrieved = 0;
int lastRotated = 0;
int rotationIndex = 0;
int tweetIndex = 0;
int currentImageCount = 0;

String currentTweetText = "";
String currentTweetTextFormatted = "";
int formattedTweetLineCount = 0;

// Display settings
bool exitButtonShown = false;
bool settingsPageShown = false;

// tjpgd
File gFile;
uint8_t gbuf[4096] __attribute__((__aligned__(4)));

// Arduino MKR1000 pins
// Display pins 
#define TFT_DC 7
#define TFT_RST 1

// Chip Select pins 
#define TFT_CS 6
#define TS_CS 2
#define SD_CS 3

// For 2.8 TFT SPI 240x320 V1.2 
// Based on testing touchscreen limits
int MIN_X = 316;
int MIN_Y = 221;
int MAX_X = 3935;
int MAX_Y = 3841;
int fourButtonLen = 80;
int fiveButtonLen = 100;
int specialOffset = 20;

// Set up screens
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS); // can set a second param for interrupts


// Touchscreen keyboard 
char userInput[26];
bool enterAndFlushInput = false;
int inputIndex = 0;
bool isCaps = false;

int bottomEdge = 240;
int buttonHeight = 25;
int rightEdge = 320;
int regOffset = 10;
int buttonWidth = 30;

int RowMap[6] = {0, 0, 0, 0, 0, 0};
int RegColMap[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int BackRowColMap[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MAX_X};
int SpecialRowColMap[4] = {0, 0, 0, 0};

// Map of minx, maxx, miny, maxy
int exitMap[4] = {0, 0, 0, 0};
int userMap[4] = {0, 0, 0, 0};
int wifiMap[4] = {0, 0, 0, 0};

const char UpperLetters[4][10] PROGMEM = {
  {'!', '@', '#', '$', '%', '^', '&', '*', '(', ')'},
  {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
  {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ' '},
  {'Z', 'X', 'C', 'V', 'B', 'N', 'M', '.', '_', '?'},
};

const char LowerLetters[4][10] PROGMEM = {
  {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
  {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
  {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ' '},
  {'z', 'x', 'c', 'v', 'b', 'n', 'm', '.', '-', '?'},
};

// WiFi
WiFiServer server(80);
WiFiClient clientOther;
WiFiClient client;
int status = WL_IDLE_STATUS;

// Setup all prereqs
void setup() {
  /*Serial.begin(38400);
  while (!Serial) {
  }
  Serial.println("setup");*/
  

  // These use active low so set to high to temporarily deactivate them 
  pinMode(TFT_CS, OUTPUT);
  pinMode(TS_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TS_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  // Attempt to read in wifi info from SD card first 
  if (!SD.begin(SD_CS)) {
    displayBasicMessage("Could not init SD card. Please make sure SD card is inserted and restart.");
    while (true) {
    }
  }
  
  // Activate the screen
  tft.begin();
  
  // Set which way the screen is oriented
  tft.setRotation(3);

  // Map the pixels to touch spots on the screen 
  createKeyboardMapping();
  createSettingsMapping();

  // Activate the touchscreen
  ts.begin();

  // Try to connect to WiFi
  connectAndPrompt();

  // Get bearer token
  initTwitterAuth();

  // Retrieve existing or set new user
  initTwitterUser();
}

// Run forever
void loop() {
  // Listen for incoming clients
  if (accountID != "") {
    int currentTime = millis();

    // Every 5 minutes, check for a new tweet
    if ((currentTime - lastRetrieved) > 300000 || lastRetrieved == 0) {
      getLatestTweet();
      lastRetrieved = currentTime;
    }

    // Every 20 seconds, cycle to next image if any
    if ((currentTime - lastRotated) > 20000)  {

      if (rotationIndex >= currentImageCount) {
        rotationIndex = 0;
      } else {
        rotationIndex += 1;
      }

      // If there is attached media, render next item in rotation
      // If there is no attached media, don't unnecessarily re-render displayed tweet text
      if (currentImageCount > 0) {
        if (rotationIndex == 0) {
          displayTweet();
        } else {
          displayImage(rotationIndex);
        }

        lastRotated = currentTime;
      }
    }
  }

  // If user pressed the screen, determine action to take if any
  if (ts.touched()) {
    standardButtonPress();
  }
}

// Draw button given starting pixel, width, height, and filler color
void createButton(int x, int y, int w, int h, unsigned int borderColor, unsigned int keyColor) {
  // Outline button in white
  tft.fillRect(x, y, w, h, borderColor);

  // Fill in button with desired color
  tft.fillRect(x+1, y+1, w - 2, h - 2, keyColor);
}

void createSettingsMapping() {
  // Exit Button
  exitMap[0] = map(1, 0, rightEdge, MIN_X, MAX_X);
  exitMap[1] = map(19, 0, rightEdge, MIN_X, MAX_X);
  exitMap[2] = map(1, 0, bottomEdge, MIN_Y, MAX_Y);
  exitMap[3] = map(19, 0, bottomEdge, MIN_Y, MAX_Y); 

  // Update User 
  userMap[0] = map(40, 0, rightEdge, MIN_X, MAX_X);
  userMap[1] = map(280, 0, rightEdge, MIN_X, MAX_X);
  userMap[2] = map(40, 0, bottomEdge, MIN_Y, MAX_Y);
  userMap[3] = map(65, 0, bottomEdge, MIN_Y, MAX_Y); 

  // Update WiFi 
  wifiMap[0] = map(40, 0, rightEdge, MIN_X, MAX_X);
  wifiMap[1] = map(280, 0, rightEdge, MIN_X, MAX_X);
  wifiMap[2] = map(80, 0, bottomEdge, MIN_Y, MAX_Y);
  wifiMap[3] = map(105, 0, bottomEdge, MIN_Y, MAX_Y); 
}

// Create a mapping of pixel to touchscreen values for each button
// so that we can quickly determine which button was pressed
void createKeyboardMapping() {
  
  // Build a row map from the bottom edge up 
  int yPixel = bottomEdge;
  for (int r = 5; r > -1; r--) {
    RowMap[r] = map(yPixel, 0, bottomEdge, MIN_Y, MAX_Y);
    yPixel = yPixel - buttonHeight;
  }

  // Find mapping for regular rows
  int xPixel = regOffset;
  for (int c = 0; c < 11; c++) {
    RegColMap[c] = map(xPixel, 0, rightEdge, MIN_X, MAX_X);
    xPixel = xPixel + buttonWidth;
  }

  // Find mapping for backspace rows
  xPixel = 0;
  for (int c = 0; c < 10; c++) {
    BackRowColMap[c] = map(xPixel, 0, rightEdge, MIN_X, MAX_X);
    xPixel = xPixel + buttonWidth;
  }

  SpecialRowColMap[0] = map(specialOffset, 0, rightEdge, MIN_X, MAX_X);
  SpecialRowColMap[1] = map(specialOffset + fourButtonLen, 0, rightEdge, MIN_X, MAX_X);
  SpecialRowColMap[2] = map(specialOffset + fourButtonLen + fiveButtonLen, 0, rightEdge, MIN_X, MAX_X);
  SpecialRowColMap[3] = map(specialOffset + fourButtonLen + (fiveButtonLen * 2), 0, rightEdge, MIN_X, MAX_X);
}

// Render keyboard on the screen
void createKeyboard(const char type[][10]){
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_BLACK);

  // Iterate through each row 
  char spaceChar = ' ';
  int topStart = 115;
  for (int r = 0; r < 4; r++) {
    int leftStart = regOffset;
    // Do things differently for this row to leave room for backspace
    if (r == 2) {
      leftStart = 0;
    }

    // Iterate through columns (items in array)
    for (int c = 0; c < 10; c++) {
      if (pgm_read_byte(&(type[r][c])) == spaceChar) {
        createButton(leftStart + (c * buttonWidth), topStart, 50, buttonHeight, ILI9341_WHITE, 0xC618);
        tft.setCursor(leftStart + 15 + (c * buttonWidth), topStart + 5);
        tft.print(F("BK"));
      } else {
        createButton(leftStart + (c * buttonWidth), topStart, buttonWidth, buttonHeight, ILI9341_WHITE, 0xC618);
        tft.setCursor(leftStart + 10 + (c * buttonWidth), topStart + 5);
        tft.print(char(pgm_read_byte(&(type[r][c]))));
      }
    } 
    topStart = topStart + buttonHeight;
  }

  // Caps 
  tft.setTextColor(ILI9341_BLACK);
  if (isCaps) {
    createButton(20, topStart, 80, buttonHeight, ILI9341_WHITE, ILI9341_RED);
  } else {
    createButton(20, topStart, 80, buttonHeight, ILI9341_WHITE, 0xC618);
  }
  tft.setCursor(35, topStart + 5);
  tft.print(F("CAPS"));

  // Space Bar 
  tft.setTextColor(ILI9341_BLACK);
  createButton(100, topStart, 100, buttonHeight, ILI9341_WHITE, 0xC618);
  tft.setCursor(120, topStart + 5);
  tft.print(F("SPACE"));

  // Enter 
  tft.setTextColor(ILI9341_BLACK);
  createButton(200, topStart, 100, buttonHeight, ILI9341_WHITE, 0xC618);
  tft.setCursor(220, topStart + 5);
  tft.print(F("ENTER"));
}

// Handle button presses on tweet display or settings page 
void standardButtonPress() {
  TS_Point p = ts.getPoint();

  if (exitButtonShown) {
    // Exit was pressed 
    if ((p.x >= exitMap[0]) && (p.x <= exitMap[1]) && (p.y >= exitMap[2]) && (p.y <= exitMap[3])) {
      if (settingsPageShown) {
        // Return to tweet display
        settingsPageShown = false;
        displayTweet();
      } else {
        // Render settings page
        createSettingsPage();
        settingsPageShown = true;
      }
    } else {
      if (settingsPageShown) {
        if ((p.x >= userMap[0]) && (p.x <= userMap[1]) && (p.y >= userMap[2]) && (p.y <= userMap[3])) {
          // Allow input of new user
          SD.remove("user");
          initTwitterUser();
          getLatestTweet();
        } else if ((p.x >= wifiMap[0]) && (p.x <= wifiMap[1]) && (p.y >= wifiMap[2]) && (p.y <= wifiMap[3])) {
          // Allow input of new WiFi info
          SD.remove("ssid");
          SD.remove("pass");
          connectAndPrompt();
          displayTweet();
        }
      }
    }
  }
}

// Determine which keyboard "key" was pressed and handle
// Will print the char to screen,
// erase the last char (Backspace),
// pass all preceding input to the caller (Enter),
// or switch keyboard (Caps)
void keyboardButtonPress() {
  int textLimit = 25;
  
  // Get coordinates of point of contact
  TS_Point p = ts.getPoint();

  // Determine the row the button is in
  int buttonRow = 0;
  int buttonCol = 0;
  for (int r = 0; r < 6; r++) {
    if ((p.y >= RowMap[r]) && (p.y <= RowMap[r+1])) {
      buttonRow = r;
      break;
    } else if (p.y > MAX_Y) {
      // Should probably recalibrate on this error, but for now don't
      buttonRow = 4;
      break;
    } else if (p.y < RowMap[0]) {
      // The point of contact was above the keyboard, ignore
      buttonRow = -1; 
      break;
    }
  }

  if (buttonRow == 2) {
    // This is the backspace row 
    for (int c = 0; c < 10; c++) {
      // Technically two edge cases here since it touches both sides
      if ((p.x >= BackRowColMap[c]) && (p.x <= BackRowColMap[c+1])) {
        buttonCol = c;
        break;
      } else if (p.x > MAX_X) {
        // Should probably recalibrate on this error, but for now don't
        buttonCol = 9;
        break;
      }
      // Dont need to define an else if for lower than MIN_X because buttonCol is already set to 0
    }
  } else if (buttonRow == 4) {
    // This is the special buttons row
    for (int c = 0; c < 3; c++) {
      if ((p.x >= SpecialRowColMap[c]) && (p.x <= SpecialRowColMap[c+1])) {
        buttonCol = c;
        break;
      } else if (p.x > SpecialRowColMap[3]) {
        // Should probably recalibrate on this error, but for now don't
        buttonCol = -2;
        break;
      } else if (p.x < SpecialRowColMap[0]) {
        buttonCol = -1;
        break;
      }
    }
  } else {
    // This is a regularly formatted row
    for (int c = 0; c < 10; c++) {
      if ((p.x >= RegColMap[c]) && (p.x <= RegColMap[c+1])) {
        buttonCol = c;
        break;
      } else if (p.x > RegColMap[10]) {
        // Should recalibrate
        buttonCol = -2;
        break;
      } else if (p.x < RegColMap[0]) {
        buttonCol = -1;
        break;
      }
    }
  }
  
  if ((buttonCol == -1) || (buttonRow == -1)) {
    // Not a valid button push 
  } else {
    if (buttonRow == 4) {
      // Special actions
      if (buttonCol == 0) {
        if (isCaps == false) {
          isCaps = true;
          createKeyboard(UpperLetters);
        } else {
          isCaps = false;
          createKeyboard(LowerLetters);
        }
      } else if (buttonCol == 1) {
        // this is a space 
        if (inputIndex < textLimit) {
          userInput[inputIndex] = ' ';
          inputIndex = inputIndex + 1;
        }
      } else if (buttonCol == 2) {
        // this is Enter
        enterAndFlushInput = true;
      }
    } else if ((buttonRow == 2) && (buttonCol == 9)) {
      // This is a backspace 
      if (inputIndex > 0) {
        tft.fillRect((inputIndex-1)*15, 40, 15, 15, ILI9341_BLACK);
        inputIndex = inputIndex - 1;
        userInput[inputIndex] = ' ';
      }
    } else {
      // this is a regular char
      if (inputIndex < textLimit) {
        if (isCaps) {
          userInput[inputIndex] = char(pgm_read_byte(&(UpperLetters[buttonRow][buttonCol])));
        } else {
          userInput[inputIndex] = char(pgm_read_byte(&(LowerLetters[buttonRow][buttonCol])));
        }
        
        // Print the character to the screen
        printInputChar();
        inputIndex = inputIndex + 1;
      }
    }
  }

  delay(200);
}

// Print the char inputted by the user to the appropriate place on the screen
void printInputChar() {
  tft.setCursor(inputIndex*15, 40);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print(userInput[inputIndex]);
}

// Retrieve credentials for making API calls to Twitter
void initTwitterAuth() {
  String tempToken = "";
  bool usedStored = "";

  // Check for existing bearer token in file on SD card
  tempToken = readStringFromFile("token");
  
  // Set bearer token to found value
  // or prompt user to enter a token value
  if (tempToken != "") {
    bearerToken = tempToken;
  } else {
    // Start a server and serve a webpage where the user can submit their bearer token
    server.begin();
    IPAddress ip = WiFi.localIP();
    String message = "Provide a bearer token at\n" + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]) + "/bearer";
    displayBasicMessage(message);
  }

  // Wait for user input on the webpage
  while (bearerToken == "") {
    clientOther = server.available();
    if (clientOther) {
      String URI = readRequest();
      sendResponse(URI);
    }
  }

  // Save the bearer token to the file
  SD.remove("token");
  writeStringToFile(bearerToken, "token");
}

// Retrieve images attached to the tweet
void getTweetImages() { 
  int imageCount = 0;
  String imageURLString;

  // Get URLs for media objects attached to the tweet
  String request = "GET /2/tweets/" + latestTweetID + "?expansions=attachments.media_keys&media.fields=url,preview_image_url";
  JSONVar responseJSON = getJSONFromTwitter(request);
  JSONVar responseIncludes = responseJSON["includes"];
  JSONVar responseMedia = responseIncludes["media"];
  
  int arrSize = responseMedia.length();

  // For each media object attached to the tweet, 
  // if it is a regular image JPEG or video preview JPEG,
  // call API to convert from progressive to baseline JPEG 
  // and then store on SD card
  for (int i = 0; i < arrSize; i++) {
    if (JSON.stringify(responseMedia[i]["type"]) == "\"photo\"") {
      imageURLString = JSON.stringify(responseMedia[i]["url"]);
    } else if (JSON.stringify(responseMedia[i]["type"]) == "\"video\"") {
      imageURLString = JSON.stringify(responseMedia[i]["preview_image_url"]);
    }

    imageURLString = imageURLString.substring(1, imageURLString.length()-1);
    if (imageURLString.endsWith(".jpg")) {
      imageURLString = imageURLString + "?name=large";
      String baselineURL = getBaselineURL(imageURLString);
      imageCount++;

      // Send Twitter media URL to API for conversion to baseline JPEG
      getBaselineJPEG(baselineURL, imageCount);
    }
  }

  currentImageCount = imageCount;
}

// Check for a new tweet from selected user
void getLatestTweet(){
  // Construct and send the request to the timeline endpoint
  // The minimum number of tweets that can be retrieved is 5
  // Exclude replies and retweets
  String request = "GET /2/users/" + accountID + "/tweets?exclude=retweets,replies&max_results=5&media.fields=preview_image_url";
  JSONVar responseJSON = getJSONFromTwitter(request);

  // Parse out the ID of the most recent tweet
  JSONVar responseData = responseJSON["data"];
  String rawTargetTweetID = JSON.stringify(responseData[0]["id"]);
  String targetTweetID = rawTargetTweetID.substring(1, rawTargetTweetID.length()-1);

  // If this is a different tweet than the last one retrieved, 
  // make necessary updates
  if (targetTweetID != latestTweetID){
    rotationIndex = 0;
    lastRotated = millis();

    latestTweetID = targetTweetID;

    // Parse out the text from the tweet
    String rawLatestTweet = JSON.stringify(responseData[0]["text"]);
    String latestTweet = rawLatestTweet.substring(1, rawLatestTweet.length()-1);

    // Check for media links and remove them from the text body
    bool hasMedia = false;
    if (latestTweet.lastIndexOf("https://t.co") != -1) {
      hasMedia = true;

      // Remove media links from the text one at a time
      while (latestTweet.indexOf("https://t.co") != -1) {
        if (latestTweet.indexOf("https://t.co") == 0) {
          // There is no plain text in the tweet, just image or video links
          latestTweet = "";
        } else {
          latestTweet = latestTweet.substring(0, latestTweet.lastIndexOf("https://t.co")-1);
        }
      }
    } else {
      currentImageCount = 0;
      rotationIndex = 0;
    }
    
    currentTweetText = latestTweet;

    // Format tweet for optimal display on screen 
    formatTweet();

    // Display the tweet then work on downloading linked media
    displayTweet();
    if (hasMedia) {
      getTweetImages();
    }
  }
}

// Render bearer token input page or respond to user entry of token
void sendResponse(String reqInfo) {
  String requestedUsername;
  String responseText;
  bool updated = false;

  clientOther.println("HTTP/1.1 200 OK");
  clientOther.println("Content-Type: text/html");
  clientOther.println("Connection: close");
  clientOther.println();

  if (reqInfo == "/bearer") {
    // Render input 
    clientOther.println("<!DOCTYPE HTML>");
    clientOther.println("<html><head> <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/><title>\"Input Form\"</title></head><body>");
    clientOther.print("<form action=\"/token\" method=\"post\" target=\"hidden-form\">");
    clientOther.print("Bearer token: ");
    clientOther.print("<input type=\"text\" name=\"tokenValue\">");
    clientOther.print("<input type=\"submit\" value=\"Submit\">");
    clientOther.print("</form><iframe style=\"display:none\" name=\"hidden-form\"></iframe>");
    clientOther.println("</body></html>");
  } else if (reqInfo.startsWith("tokenValue") == 1) {
    // Retrieve entered token
    reqInfo.replace("%25", "%");
    bearerToken = reqInfo.substring(11);
  }

  delay(10);
  clientOther.stop();
}

// Read request info that user sent to server
String readRequest() {
  String request = "";
  String URI;
  int doneHTTP = 0;
  int spaceOne, spaceTwo;

  while (!clientOther.available()) {
    // Wait 
  }

  // Read request data
  while (clientOther.available()) {
    char character = clientOther.read();
    request = request + character;

    if (request == "\r\n") {
      doneHTTP = 1;
      continue;
    }

    if (character == '\r') {
      character = clientOther.read();

      // If this is a GET request, just return path so that appropriate page can be rendered
      if (request.startsWith("GET") == 1) {
        spaceOne = request.indexOf(" ") + 1;
        spaceTwo = request.indexOf(" ", spaceOne);
        URI = request.substring(spaceOne, spaceTwo);
        return URI;
      }

      if (doneHTTP == 0) {
        request = "";
      }
    }

    if (!clientOther.connected()) {
      clientOther.stop();
    }
  }

  // If not a GET request, retiurn last line of the request
  return(request);
}

// Make sure requested Twitter user exists
void initTwitterUser() {
  latestTweetID = "";

  // Check SD card for stored username
  String tempUsername = readStringFromFile("user");

  if (tempUsername != "") {
    if (tempUsername.startsWith("@") == 1) {
      tempUsername = tempUsername.substring(1);
    }

    // Look for accountID associated with that username
    accountID = getAccountID(tempUsername);

    // No account could be found tied to that username
    if (accountID == "") {
      displayBasicMessage("No one found with this username");
      tempUsername == "";
      delay(2000);
    }
  }
  
  // If no username is stored on the SD card, prompt user to enter one
  while (tempUsername == "") { 
    tempUsername = getKeyboardInput("Enter twitter user to\ndisplay tweets from (@):");
    
    if (tempUsername.startsWith("@") == 1) {
      tempUsername = tempUsername.substring(1);
    }
    
    accountID = getAccountID(tempUsername);
    
    if (accountID == "") {
      displayBasicMessage("No one found with this username");
      tempUsername == "";
      delay(2000);
    }
  }

  // User was successfully set
  String userMessage = "User set as " + tempUsername;
  displayBasicMessage(userMessage);

  // Store username to be used later
  SD.remove("user");
  username = tempUsername; 
  writeStringToFile(tempUsername, "user");
  writeStringToFile(tempUsername, "testfile");
  delay(2000);
}

// Read a value from a file on the SD card
String readStringFromFile(String filename) {
  String tempString = "";
  File stringFile = SD.open(filename, FILE_READ);

  if (stringFile) {
    while (stringFile.available()) {
      char character = stringFile.read();
      tempString = tempString + character;
    }
  }

  stringFile.close();
  return(tempString);
}

// Write a value to a file on the SD card
void writeStringToFile(String writeString, String filename) {
  File stringFile = SD.open(filename, FILE_WRITE);
  
  if (stringFile) {
    stringFile.print(writeString);
  }

  stringFile.close();
}

// Search for accountID for a given username
String getAccountID(String targetUsername) {
  // Contruct and send the request to the user endpoint
  String request = "GET /2/users/by/username/" + targetUsername + " HTTP/1.1";
  JSONVar responseJSON = getJSONFromTwitter(request);

  // Pull ID from response
  if (responseJSON.hasOwnProperty("data")) {
    JSONVar responseData = responseJSON["data"];
    String rawID = JSON.stringify(responseData["id"]);
    String userID = rawID.substring(1, rawID.length()-1);
    return(userID);
  } else {
    return("");
  }
}

// Get download URL for baseline JPEG
String getBaselineURL(String twitterURL) {
  String response;
  String headers;
  int doneHTTP = 0;
  String URI = "GET /v1/jpeg_optim.php?url=" + twitterURL + "&encoding=baseline HTTP/1.1";
  
  // Send request to conversion API
  if (client.connect("api.rest7.com", 80)) {
    client.println(URI);
    client.println("Host: api.rest7.com");
    client.println("Connection: close");
    client.println();
  }

  while (!client.available()) {
    // Wait for return 
  }

  while (client.available()) {
    char c = client.read(); // read input a byte at a time
    if (doneHTTP == 0) { 
      headers = headers + c;
      if (c == '\r') { // this is the end of a line 
        char c = client.read();
        headers = headers + c;
        if (c == '\n') {
          if (headers.equals("\r\n")) {
            doneHTTP = 1;
          } else {
            headers = "";
          }
        }
      }
    } else {
      if (c != '\\') {
        response = response + c;
      }
    }
  }

  if (!client.connected()) {
    client.stop();
  }

  JSONVar responseJSON = JSON.parse(response);
  int success = responseJSON["success"];
  
  if (success == 1) {
    String fileURL = JSON.stringify(responseJSON["file"]);
    return fileURL;
  } else {
    return("Failed");
  }
}

// Get baseline JPEG from download URL
void getBaselineJPEG(String imageURL, int photoIndex) {
  bool slice = imageURL.startsWith("\"http://api.rest7.com");

  if (slice) {
    imageURL = imageURL.substring(21, imageURL.length()-1);
  }

  String URI = "GET " + imageURL + " HTTP/1.1";
  
  // Send request to conversion API
  if (client.connect("api.rest7.com", 80)) {
    client.println(URI);
    client.println("Host: api.rest7.com");
    client.println("Connection: close");
    client.println();
  }

  String photoFile = "JPEGS/PHOTO_" + String(photoIndex) + ".JPG";
  boolean readChars = true;
  
  SD.remove(photoFile);
  delay(1000);
  
  File myFile = SD.open(photoFile, FILE_WRITE);

  byte dataBuffer [1000];
  boolean readingHeaders = true;
  String previous = "";
  char charBuffer [200];
  int counter = 0;
  int headerIndex = 0;
  int contentBytes = 0;
  int bytesRead = 0;

  // Write JPEG to file on SD card
  if (myFile) {
    while (readChars) {
      if (client.available()) {
        if (readingHeaders) {
          char c = client.read();
          if (c == '\r') {
            if (previous == "rn") {
              previous = "rnr";
            } else {
              previous = "r";
            }

          } else if (c == '\n') {
            charBuffer[headerIndex] = '\0';
            String headerString = String(charBuffer);
            int lengthIndex = headerString.indexOf("Content-Length");
            
            if (lengthIndex != -1) {
              int spaceIndex = headerString.indexOf(' ');
              String contentLengthString = headerString.substring(spaceIndex+1);
              contentBytes = contentLengthString.toInt();
            }

            headerIndex = 0;
            
            if (previous == "rnr") {
              readingHeaders = false;
            } else if (previous == "r") {
              previous = "rn";
            }
          } else {
            charBuffer[headerIndex] = c;
            headerIndex++;
            previous = "";
          }
        } else {
          byte b = client.read();
          dataBuffer[counter] = b;
          counter++;
          bytesRead++;
          
          if (counter == 1000) {
            myFile.write(dataBuffer, 1000);
            counter = 0;
          }
          
          if (bytesRead == contentBytes) {
            myFile.write(dataBuffer, counter);
            readChars = false;
            myFile.close();
          }
        }
      }
      
      if (!client.available() && !client.connected()) {
        readChars = false;
      }
    }
  }
  
  myFile.close();
}

// Get JSON object from some Twitter endpoint
JSONVar getJSONFromTwitter(String request) {
  String response;
  String authHeader = "Authorization: Bearer " + bearerToken;
  
  if (client.connectSSL("api.twitter.com", 443)) {
    // Send HTTP info to the server
    client.println(request);
    client.println("Host: api.twitter.com");
    client.println(authHeader);
    client.println("Connection: close");
    client.println();
  }

  while (!client.available()) {
    // Wait for return 
  }

  while (client.available()) {
    char c = client.read(); // Read input a byte at a time 
    response = response + c; // Add to existing string

    if (response.equals("\r\n")) {
      continue; 
    }
    
    if (c == '\r') { // this is the end of a line 
      c = client.read(); // read and throw away the \n
    }
  }

  if (!client.connected()) {
    client.stop();
  }

  // Pull only JSON from response
  int firstBracket = response.indexOf('{');
  int lastBracket = response.lastIndexOf('}');
  String responseBody = response.substring(firstBracket, lastBracket + 1);

  // Parse the JSON and return
  JSONVar responseJSON = JSON.parse(responseBody);
  return(responseJSON);
}

// Try to connect to WiFi 
// Prompt user for WiFi connection info as needed
void connectAndPrompt() { 
  bool usedStored = true;

  // Try to pull WiFi info from SD card
  String tempWifiNetworkString = readStringFromFile("ssid");
  String tempWifiPasswordString = readStringFromFile("pass");

  // No stored WiFi fields 
  // Prompt user to enter WiFi info
  if ((tempWifiPasswordString == "") || (tempWifiNetworkString == "")) {
    usedStored = false;
    tempWifiNetworkString = getKeyboardInput("Please enter your WiFi\nnetwork name:");
    tempWifiPasswordString = getKeyboardInput("Please enter your WiFi\npassword:");
  }

  // Convert to char arrays to connect to wifi using WifiNINA
  int ssidLen = tempWifiNetworkString.length() + 1;
  int passLen = tempWifiPasswordString.length() + 1;
  char ssid[ssidLen];
  char pass[passLen];
  tempWifiNetworkString.toCharArray(ssid, ssidLen);
  tempWifiPasswordString.toCharArray(pass, passLen);

  // Keep track of how much time has been spent trying to connect
  // so that user can be re-prompted for wifi creds if connection fails
  int timeCounter = 0;
  
  // Try to connect to the wifi
  status = WiFi.begin(ssid, pass);

  // Arduino has not connected to WiFi yet
  while (status != WL_CONNECTED) {
    // If connection has not succeeded after around 1 minute,
    // reprompt user for wifi creds
    if (timeCounter >= 6) {
      displayBasicMessage("Failed to connect!");
      delay(3000);
      
      // Prompt user to enter wifi info
      usedStored = false;
      tempWifiNetworkString = getKeyboardInput("Please enter your WiFi\nnetwork name:");
      tempWifiPasswordString = getKeyboardInput("Please enter your WiFi\npassword:");

      // Convert to char arrays to connect to wifi using WifiNINA
      ssidLen = tempWifiNetworkString.length() + 1;
      passLen = tempWifiPasswordString.length() + 1;
      char ssid[ssidLen];
      char pass[passLen];
      tempWifiNetworkString.toCharArray(ssid, ssidLen);
      tempWifiPasswordString.toCharArray(pass, passLen);

      // Try to connect to WiFi
      status = WiFi.begin(ssid, pass);
      
      // Reset timeCounter
      timeCounter = 0;
      
    } else {
      if (timeCounter == 0) {
        // While the WiFi is trying to connect, display connecting screen
        displayBasicMessage("Connecting...");
      }
      
      // Wait 10 seconds before checking connection again
      delay(10000);
      timeCounter += 1;
    }
  }

  // If the ssid + password used to successfully connect were not from the SD card,
  // store them on the SD card to be used next time
  if (!usedStored) {
    SD.remove("ssid");
    writeStringToFile(tempWifiNetworkString, "ssid");

    SD.remove("pass");
    writeStringToFile(tempWifiPasswordString, "pass");
  }

  displayBasicMessage("Connected!");
  delay(2000);
}

// Print a basic message on a black screen
void displayBasicMessage(String message) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.setFont();
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.println(message);
}

// Display the text of the tweet on the screen 
void displayTweet() {
  tft.fillScreen(ILI9341_WHITE);
  createExitButton(0xC618, ILI9341_WHITE);
  String tweetDisplay = "@" + username;

  // Likely this tweet is all media. Just display the username
  if (currentTweetText == "") {
    tft.setFont(&FreeSans12pt7b);
    // Screen about 23 of these font/size chars across
    int dif = 23 - tweetDisplay.length();
    int leftMark = 5.5 * dif;
    tft.setCursor(leftMark, 120);
    
  } else {
    // Display the username and tweet text
    int topCursor = 30 + ((10 - formattedTweetLineCount) * 10);
    tft.setCursor(0, topCursor);
    tft.setFont(&FreeSans9pt7b);
    tweetDisplay = currentTweetTextFormatted;
  }

  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(1);
  tft.println(tweetDisplay);
}

// Determine format for displaying tweet text based on length of tweets and words, etc
void formatTweet() {
  String displayTweet = "@" + username + ": " + currentTweetText;
  int tweetLength = displayTweet.length() + 1;
  char tweet[tweetLength];
  displayTweet.toCharArray(tweet, tweetLength);

  String finalTweet = "";
  String thisLine;
  String nextLine = "";

  int counter = 0;
  int lastSpace = 0;
  int lineCount = 1;
  bool lastNewline = false;

  for (int i = 0; i < tweetLength; i++) {
    counter++;
    bool newline = false;

    // Check for newline; Treat as such
    if (tweet[i] == '\\') {
      if (i+1 < tweetLength) {
        if (tweet[i+1] == 'n') {
          i++;
          counter--;
          newline = true;
        }
      }
    } else {
      // Try to only break to a new line on a space
      if (tweet[i] == ' ') {
        lastSpace = i;
        thisLine = thisLine + " " + nextLine;
        nextLine = "";
      } else {
        nextLine = nextLine + tweet[i];
      }
      
      // @ is wide so count as 2 chars
      if (tweet[i] == '@') {
        counter++;
      }
    }
    
    // New line should begin
    if (counter == 38 || (newline && !lastNewline)) {
      if (lineCount != 1) {
        finalTweet = finalTweet + "\n";
      }

      if (lastSpace == 0) {
        finalTweet = finalTweet + nextLine;
        nextLine = "";
      } else {
        finalTweet = finalTweet + thisLine;
      }

      lineCount++;
      counter = i - lastSpace;
      thisLine = "";
    }

    if (newline) {
      lastNewline = true;
    } else {
      lastNewline = false;
    }
  }

  if (counter > 0) {
    if (lineCount == 1) {
      finalTweet = thisLine;
    } else {
      finalTweet = finalTweet + "\n" + thisLine;
    }
  
    if (tweetLength-1 != lastSpace) {
      finalTweet = finalTweet + " " + nextLine;
    }  
    lineCount++;
  }

  currentTweetTextFormatted = finalTweet;
  formattedTweetLineCount = lineCount;
}

// Render an X in the top left corner for exiting a given screen
void createExitButton(unsigned int borderColor, unsigned int textColor) {
  createButton(0, 0, 20, 20, borderColor, ILI9341_RED);
  tft.setFont();
  tft.setCursor(5,1);
  tft.setTextColor(textColor);
  tft.setTextSize(2);
  tft.println("x");
  exitButtonShown = true;
}

// Render settings page for changing target user or WiFi info
void createSettingsPage() {
  tft.fillScreen(ILI9341_BLACK);
  createExitButton(0xC618, ILI9341_WHITE);

  // Update user 
  tft.setFont();
  createButton(40, 40, 240, 25, ILI9341_WHITE, 0xC618);
  tft.setCursor(50, 45);
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.println("Update User");

  createButton(40, 80, 240, 25, ILI9341_WHITE, 0xC618);
  tft.setCursor(50, 85);
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.println("Update WiFi Info");
}

// Display a message prompt and render a keyboard to get user input
String getKeyboardInput(String message) {
  // Prompt message
  displayBasicMessage(message);

  // Render the keyboard
  createKeyboard(LowerLetters);
  delay(100);

  // Get user input
  while (!enterAndFlushInput) {
    if (ts.touched()) {
      keyboardButtonPress();
    }
  }

  // The while loop was exited which means 'Enter' was pressed 
  // Put a termination char at end so intented String is created from the array
  userInput[inputIndex] = '\0';
  String inputString = userInput;

  // Clear out the previous values from the input array so it can be reused
  for (int i = 0; i < 25; i++) {
    userInput[i] = ' ';
  }
  enterAndFlushInput = false;
  inputIndex = 0;

  return(inputString); 
}

/*
==========================================================
Everything below adapted from show_tjpgd_SD_8357
==========================================================
*/

void displayImage(int imageIndex) {
  String photo = "JPEGS/PHOTO_" + String(imageIndex) + ".JPG";
  char __photo[18];
  photo.toCharArray(__photo, 18);
  load_jpg(__photo, gbuf, sizeof(gbuf));
}

void pushImage(int x, int y, int w, int h, uint16_t *pImg) {
  int n = w * h;   
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.writePixels(pImg, n);
  tft.endWrite();
}

static uint16_t tjd_input(JDEC* jd, uint8_t* buff, uint16_t nd) {
  uint16_t rb = nd;

  jd = jd;    /* Suppress warning (device identifier is not needed in this application) */

  if (buff) { /* Read nd bytes from the input strem */
    if (gFile.read(buff, nd) < 0) rb = 0;
    return (uint16_t)rb;    /* Returns number of bytes could be read */
  } else {    /* Skip nd bytes on the input stream */
    return (gFile.seek(gFile.position() + nd) == true) ? nd : 0;
  }
}

/* User defined call-back function to output RGB bitmap */
static uint16_t tjd_output(JDEC* jd, void* bitmap, JRECT* rect, int* topOffset, int* leftOffset) {
  jd = jd;    /* Suppress warning (device identifier is not needed in this appication) */

  /* Put the rectangle into the display device */
  pushImage(rect->left + *leftOffset, rect->top + *topOffset,
            rect->right - rect->left + 1, rect->bottom - rect->top + 1,
            (uint16_t*)bitmap);
  return 1;   /* Continue to decompression */
}

void load_jpg(const char* fn, void *work, uint16_t sz_work) {
  JDEC jd;        /* Decompression object (70 bytes) */
  JRESULT rc;
  uint8_t scale;
  char buf[40];

  gFile = SD.open(fn);
  if (!gFile) {
    return;
  }

  /* Prepare to decompress the file */
  rc = jd_prepare(&jd, tjd_input, work, sz_work, 0);
  if (rc == JDR_OK) {
    tft.fillScreen(ILI9341_WHITE);
    createExitButton(0xC618, ILI9341_WHITE);
    uint32_t t = millis();
    
    /* Determine scale factor */
    for (scale = 0; scale < 3; scale++) {
      if ((jd.width >> scale) <= tft.width() && (jd.height >> scale) <= tft.height()) break;
    }

    int scaledwidth = jd.width >> scale;
    int scaledheight = jd.height >> scale;
    int topOffsetInt = (tft.height() - scaledheight) / 2;
    int leftOffsetInt = (tft.width() - scaledwidth) / 2;

    int *topOffset = &topOffsetInt;
    int *leftOffset = &leftOffsetInt;

    /* Start to decompress the JPEG file */
    rc = jd_decomp(&jd, tjd_output, scale, topOffset, leftOffset);
    t = millis() - t;
  }
  
  gFile.close();
  delay(5000);
}
