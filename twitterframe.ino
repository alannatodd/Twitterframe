#include <SPI.h>
#include <WiFiNINA.h>
#include <Arduino_JSON.h>

char ssid[] = "";
char pass[] = "";
String username = "";
String accountID = "";
String latestTweetID = "";
int lastRetrieved = 0;

int status = WL_IDLE_STATUS;

WiFiServer server(80);
WiFiClient clientOther;
WiFiClient client;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  while (status != WL_CONNECTED){
    status = WiFi.begin(ssid, pass);
    // Wait 5 seconds for connection 
    delay(5000);
  }

  server.begin();
  printWifiStatus();
  
  //accountID = getUserID("jaboukie"); 
  //Serial.println(accountID);
  Serial.print("Please indicate an account by going to: ");
  Serial.print(WiFi.localIP());
  Serial.println("/<username>");
}

void loop() {
  // listen for incoming clients
  clientOther = server.available();
  if (clientOther) {
    // an HTTP request ends with a blank line 
    String URI = readRequest();
    bool updated = sendResponse(URI);
    if (updated) {
      String userIDResponse = getUserID();
      if (userIDResponse == "") {
      } else {
        accountID = userIDResponse;
        getLatestTweet();
      }
    }
  }
  if (accountID != "") {
    int currentTime = millis();
    if ((currentTime - lastRetrieved) > 60000 || lastRetrieved == 0) {
      getLatestTweet();
      lastRetrieved = currentTime;
    }
  }
}

String readRequest(){
  String request = "";
  int doneHTTP = 0;
  int spaceOne,spaceTwo;
  String URI;

  while (!clientOther.available()){
    // wait until stuff is returned
  }

  while (clientOther.available()) {
    char c = clientOther.read();
    request = request + c; 

    if (request.equals("\r\n")) {
      doneHTTP = 1;
      continue;
    }

    if (c == '\r') {
      c = clientOther.read();

      if (request.startsWith("GET") == 1) {
        spaceOne = request.indexOf(" ") + 1;
        spaceTwo = request.indexOf(" ",spaceOne);
        URI = request.substring(spaceOne,spaceTwo);
      }

      if (doneHTTP == 0) {
        request = "";
      }
    }
  }
  
  if (!clientOther.connected()) {
    clientOther.stop();
  }
  
  return(URI);
}

bool sendResponse(String URI) {
  String requestedUsername;
  String responseText;
  bool updated = false;
  
  clientOther.println("HTTP/1.1 200 OK");
  clientOther.println("Content-Type: text/html");
  clientOther.println("Connection: close");
  clientOther.println();
  
  if (URI == "/") {
    responseText = "No username provided. Please provide a username";
  } else {
    requestedUsername = URI.substring(1);
    if (requestedUsername == username) {
      responseText = "No update to make, this username is already set";
    } else {
      username = requestedUsername;
      responseText = "Success! Username updated";
      updated = true;
    }
  }

  //clientOther.println("Content-type:text/html");
  //clientOther.println("<!DOCTYPE HTML>");
  clientOther.print("<html><head></head><body>Here</body></html>");
  //clientOther.print("<h2>Success!</h2>");
  //clientOther.print(responseText);
  //clientOther.println("</h2>");
  //clientOther.print("</body></html>");
  clientOther.println();
  
  delay(10);

  clientOther.stop();

  return(updated);
}

void getLatestTweet(){
  // Construct and send the request to the timeline endpoint
  // The minimum number of tweets that can be retrieved is 5
  // For now exclude replies and retweets
  String request = "GET /2/users/" + accountID + "/tweets?exclude=retweets,replies&max_results=5&media.fields=preview_image_url";
  JSONVar responseJSON = getJSONFromTwitter(request);
  //Serial.println(responseJSON);
  JSONVar responseData = responseJSON["data"];
  String rawTargetTweetID = JSON.stringify(responseData[0]["id"]);
  String targetTweetID = rawTargetTweetID.substring(1, rawTargetTweetID.length()-1);
  if (targetTweetID != latestTweetID){
    latestTweetID = targetTweetID;
    String rawLatestTweet = JSON.stringify(responseData[0]["text"]);
    String latestTweet = rawLatestTweet.substring(1, rawLatestTweet.length()-1);
    
    // For now, just strip out image urls
    while (latestTweet.indexOf("https://t.co") != -1) {
      latestTweet = latestTweet.substring(0, latestTweet.lastIndexOf("https://t.co")-1);
    }
    String fullText = "@" + username + ": " + latestTweet;
    Serial.println(fullText);
  }
}

void printLatestTweet(){
  // Construct and send the request to the tweets endpoint
  String request = "GET /2/tweets/" + latestTweetID;
  JSONVar responseJSON = getJSONFromTwitter(request);
  JSONVar responseData = responseJSON["data"];
  
  Serial.println(JSON.stringify(responseJSON));
}

String getUserID() {
  // Contruct and send the request to the user endpoint
  String request = "GET /2/users/by/username/" + username + " HTTP/1.1";
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

JSONVar getJSONFromTwitter(String request) {
  String response;
  int doneHTTP = 0;
  
  if (client.connectSSL("api.twitter.com", 443)) {
    // Send HTTP info to the server
    client.println(request);
    client.println("Host: api.twitter.com");
    client.println("Authorization: Bearer [token]");
    client.println("Connection: close");
    client.println();
  }

  while (!client.available()) {
    // Wait for return 
    // Serial.println("Not available");
  }

  while (client.available()) {
    //Serial.println("Reading");
    char c = client.read(); // read input a byte at a time 
    response = response + c;        // slowly building a string

    if (response.equals("\r\n")) {
      doneHTTP = 1; // if the line consists of just an \r\n
      continue; // All the http is done, the rest is payload 
    }
    
    if (c == '\r') { // this is the end of a line 
      c = client.read(); // read and throw away the \n
    }
  }

  if (!client.connected()) {
    client.stop();
  }

  int firstBracket = response.indexOf('{');
  int lastBracket = response.lastIndexOf('}');
  String responseBody = response.substring(firstBracket, lastBracket + 1);

  // Parse the JSON and return
  JSONVar responseJSON = JSON.parse(responseBody);
  return(responseJSON);
}

void printWifiStatus() {
  // print the SSID of the network you're attached to
  Serial.println("SSID: " + String(WiFi.SSID()));
}
