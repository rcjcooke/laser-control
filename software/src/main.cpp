#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "rgb_lcd.h"

// Wifi credentials recorded in this file
#include "wifiCredentials.h"

/************************
 * Constants
 ************************/
// Air Assist control pin
static const uint8_t AA_CTL = D9;
// Air Filter control pin
static const uint8_t AF_CTL = D8;
// The characters used for the "waiting" spinner on the display
static const char WAITING_CHARS[5] = "-\\|/";

// RGB Blue
static const unsigned char LCD_BLUE[] = {0, 0, 255};
// RGB Green
static const unsigned char LCD_GREEN[] = {0, 255, 0};
// RGB Red
static const unsigned char LCD_RED[] = {255, 0, 0};

/************************
 * Variables
 ************************/
// The LCD display
rgb_lcd lcd;
// Listen on socket 81 for incoming connections
WiFiServer wifiServer(81);

/************************
 * Utility functions
 ************************/
// Convenience method for assigned all RGB values at the same time
void setLCDColour(const unsigned char colour[]) {
  lcd.setRGB(colour[0], colour[1], colour[2]);
}

// Prints a spinner on the LCD, changing state every 250 ms
void updateSpinner() {
  unsigned long prevMillis = millis();
  static uint8_t spinnerCharNum = 0;
  // If it's been 250 ms since last time we checked, let us know we're still alive
  if (millis() - prevMillis >= 250) {
    lcd.setCursor(0,1);
    lcd.print(WAITING_CHARS[spinnerCharNum++]);
    if (spinnerCharNum == 4) spinnerCharNum = 0;
    prevMillis = millis();
  }
}

// Format Wi-Fi status to something human readable
String formatWiFiStatus(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD:
      return "WL_NO_SHIELD";
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN STATUS: " + status;
  }
}

// Establish a Wifi connection while providing feedback
void connectWiFi() {
  // Connect and let us know how that goes
  wl_status_t status;
  while (status != WL_CONNECTED) {
    // If the status has changed, let us know
    if (WiFi.status() != status) {
      status = WiFi.status();
      lcd.setCursor(0,0);
      lcd.println(formatWiFiStatus(WiFi.status()));
    }
    updateSpinner();
  }
}

/************************
 * Env Control functions
 ************************/

void turnOnAirExtraction() {
  digitalWrite(AF_CTL, HIGH);
}

void turnOffAirExtraction() {
  digitalWrite(AF_CTL, LOW);
}

void turnOnAirAssist() {
  digitalWrite(AA_CTL, HIGH);
}

void turnOffAirAssist() {
  digitalWrite(AA_CTL, LOW);
}

/************************
 * Entry point methods
 ************************/
void setup() {
  // Set up output control pins
  pinMode(AA_CTL, OUTPUT);
  pinMode(AF_CTL, OUTPUT);
  digitalWrite(AA_CTL, LOW);
  digitalWrite(AF_CTL, LOW);

  // Set up the LCD display (16 characters x 2 rows)
  // Start up the display
  lcd.noCursor();
  lcd.noBlink();
  lcd.begin(16, 2);  

  // Atomstack Laser Cutters communicate at a baud rate of 115200 bps
  Serial.begin(115200);
  while(!Serial); // Wait for initialisation of the serial interface

  // Set up the Wifi connection
  WiFi.begin(ssid, password);
}

void loop() {

  // The client object
  static WiFiClient client;

  // States
  const static int STATE_NO_WIFI_CONNECTION = 0;
  const static int STATE_WAITING_FOR_CLIENT_CONNECTION = 1;
  const static int GOT_CLIENT_CONNECTION = 2;
  static int state = 0;

  // Check to see if we've dropped the connection
  if (WiFi.status() != WL_CONNECTED) {
    state = STATE_NO_WIFI_CONNECTION;
  }

  switch (state) {
    case STATE_NO_WIFI_CONNECTION:
        // Let the user know it's connecting on Wifi
      setLCDColour(LCD_RED);
      lcd.clear();
      lcd.blinkLED();

      // Keep trying to establish a connection to WiFi
      connectWiFi();
      // Now we've got a connection, move to waiting for client connection
      state = STATE_WAITING_FOR_CLIENT_CONNECTION;
      break;

    case STATE_WAITING_FOR_CLIENT_CONNECTION:
      // Let the user know we're connected to Wifi and waiting
      setLCDColour(LCD_GREEN);
      lcd.noBlinkLED();
      lcd.setCursor(0,0);
      lcd.println(WiFi.localIP().toString());
      
      // Note: This (intentionally) only supports a single client connection - this
      // is not an async implementation (and that would make no sense for a laser cutter! :) )
      client = wifiServer.available();
      // Wait for a client while letting us know we're still alive
      while (!client) {
        updateSpinner();
        client = wifiServer.available();
      }
      // Now we've got a connection, move to waiting for client connection
      state = GOT_CLIENT_CONNECTION;
      break;

    case GOT_CLIENT_CONNECTION:
      // Let the user know we've got a connection
      setLCDColour(LCD_BLUE);
      lcd.noBlinkLED();
      lcd.setCursor(0,1);
      lcd.println(client.remoteIP().toString());

      if (client) {
        while (client.connected()) {
          // Read any data available and echo it out the other way
          while (client.available() > 0 || Serial.available() > 0) {
            if (client.available() > 0) {
              /*
              Note: Read a line at a time - this is a risk though so there's a
              built in timeout, default 1 second.

              Note2: The resultant string will not contain the terminating
              character.

              Note3: Depending on whether the controller and the laser cutter
              are using the streaming or the request/response protocol, the
              approach below may be breaking that. This will be tested.
              */
              String s = client.readStringUntil('\r');

              if (s) {
                if (s.equals("M4")) {
                  // G-Code: M4 - start spindle (for a laser cutter this is the start of the program)
                  // Turn on air extraction
                  turnOnAirExtraction();
                } else if (s.equals("M8")) {
                  // G-Code: M8 = turn on coolant (for a laser cutter, this is air assist)
                  // Turn on air assist
                  turnOnAirAssist();
                } else if (s.equals("M9")) {
                  // G-Code: M9 = turn off coolant (for a laser cutter, this is air assist)
                  // Turn off air assist
                  turnOffAirAssist();
                } else if (s.equals("M2")) {
                  // G-Code: M2 = end the program
                  // Turn off air extraction
                  turnOffAirExtraction();
                }

                // Pass everything on to the laser cutter
                Serial.print(s);
                Serial.write('\r');
              }
            }
            if (Serial.available() > 0) {
              char c = Serial.read();
              // Pass everything back to the controller
              client.write(c);
            }
          }
        }
      }
      // Free up client resources
      client.stop();
      // Time to get a new client
      state = STATE_WAITING_FOR_CLIENT_CONNECTION;
      break;

    default:
      setLCDColour(LCD_RED);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.println("ERROR: S " + state);
      lcd.setCursor(0,1);
      lcd.println("Pausing 30s");
      delay(300000);

      state = STATE_WAITING_FOR_CLIENT_CONNECTION;
      break;
  }

}
