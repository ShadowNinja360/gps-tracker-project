#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include "esp_system.h"

#define DEBUG_GPS   true  // Set to true to feed dummy GPS data
#define DEBUG_GPRS  false  // Set to true to simulate GPRS communication success

#include <HTTPClient.h>
#include <HardwareSerial.h>

#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h>

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
HardwareSerial gpsSerial(2);

#define SIM800L_RX_PIN 27
#define SIM800L_TX_PIN 26
#define SIM800L_RST_PIN 4
#define SIM800L_BAUDRATE 9600

#define ONBOARD_BUTTON_PIN 13

const char apn[] = "airtelgprs.com";
const char user[] = "";
const char pass[] = "";

const char *serverUrl = "https://us-central1-gps-tracker-59cb6.cloudfunctions.net/receiveGPSData";

    Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
TinyGPSPlus gps;

HardwareSerial sim800lSerial(1);
TinyGsm modem(sim800lSerial);

bool trackerActive = false;
double totalDistanceMeters = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;
unsigned long journeyID = 0;
String currentOperatingMode = "WORKOUT";

unsigned long lastDataSendMillis = 0;
const unsigned long DATA_SEND_INTERVAL_MS = 10000;

unsigned long lastDisplayUpdateMillis = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL_MS = 2000;

unsigned long lastButtonPressMillis = 0;
const unsigned long DEBOUNCE_DELAY_MS = 200;

void displayInfo();
void sendDataToServer();
void toggleTrackerState();
void generateNewJourneyID();

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting ESP32 Cyclist Tracker...");

  int resetReason = esp_rom_get_reset_reason(0); // For CPU0
  // int resetReason1 = rtc_get_reset_reason(1); // For CPU1 if needed, but CPU0 is often sufficient for general reset
  Serial.print("Reset Reason (CPU0): ");
  Serial.println(resetReason); // Print numerical reason
  Serial.println(esp_reset_reason()); // Print human-readable reason

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed. Check wiring and address."));
    for (;;)
      ;
  }
  display.display();
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Tracker Init...");
  display.display();

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS Serial Initialized on UART2.");

  sim800lSerial.begin(SIM800L_BAUDRATE, SERIAL_8N1, SIM800L_RX_PIN, SIM800L_TX_PIN);
  if (SIM800L_RST_PIN != -1) {
    pinMode(SIM800L_RST_PIN, OUTPUT);
    digitalWrite(SIM800L_RST_PIN, HIGH);
    delay(100);
    digitalWrite(SIM800L_RST_PIN, LOW);
    delay(1000);
    digitalWrite(SIM800L_RST_PIN, HIGH);
    delay(3000);
  }
  Serial.println("SIM800L Serial Initialized on UART1 (remapped).");

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Modem Init...");
  display.display();
  delay(500);

  Serial.println("Initializing modem...");

  // --- NEW: Conditionally initialize modem based on DEBUG_GPRS ---
  if (DEBUG_GPRS) {
    // In DEBUG_GPRS mode, simulate network connection
    Serial.println("DEBUG_GPRS is true: Simulating Network/GPRS connection success.");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Network OK!");
    display.println("GPRS OK!");
    display.display();
    // No actual modem.init() or network calls needed in this mode
  } else {
    // In normal mode, perform actual network connection
    modem.init(); // Initialize modem (power it on, etc.)
    Serial.print("Waiting for network...");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Network...");
    display.display();

    if (!modem.waitForNetwork(60000L)) {
      Serial.println(" Failed to connect to network!");
      display.println("Network Fail!");
      display.display();
    } else {
      Serial.println(" Network connected.");
      display.println("Network OK!");
      display.display();
    }
    delay(1000); // Give modem time to settle

    Serial.print("Attaching GPRS... ");
    if (!modem.gprsConnect(apn, user, pass)) {
      Serial.println(" Failed to attach GPRS!");
      display.println("GPRS Fail!");
      display.display();
    } else {
      Serial.println(" GPRS attached.");
      display.println("GPRS OK!");
      display.display();
    }
    delay(1000);
  }
  pinMode(ONBOARD_BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button Ready (GPIO0 or GPIO13).");

  generateNewJourneyID();
  Serial.print("Initial Journey ID: ");
  Serial.println(journeyID);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Mode: " + currentOperatingMode);
  display.println("Press button to start.");
  display.display();
}

// --- New Function: Trigger Emergency Alert ---
void triggerAlert() {
  Serial.println("!!! ALERT TRIGGERED in Personal Safety Mode !!!");
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("!!! ALERT !!!");
  display.println("Sending Help Signal");
  display.display();

  // Assuming GPS has valid data from loop()
  String alertLat = String(gps.location.lat(), 6);
  String alertLon = String(gps.location.lng(), 6);

  // --- Send Alert SMS (Example) ---
  // You would need to refine this to send to actual emergency numbers
  if (modem.isGprsConnected()) { // Or check for network registration
      String smsMessage = "SOS! I need help! My location: https://maps.google.com/?q=" + alertLat + "," + alertLon;
      Serial.println("Sending SMS: " + smsMessage);
      if (modem.sendSMS("+91XXXXXXXXXX", smsMessage)) { // REPLACE with your emergency number
          Serial.println("SMS Alert Sent!");
          display.println("SMS Sent!");
          display.display();
      } else {
          Serial.println("SMS Alert Failed!");
          display.println("SMS Failed!");
          display.display();
      }
  } else {
      Serial.println("Cannot send SMS: No GPRS/Network.");
      display.println("No Network!");
      display.display();
  }
  
  // --- Optionally, send a specific ALERT flag to Firebase too ---
  // This would require modifying sendDataToServer to take an optional "alert" parameter
  // or sending a separate HTTP POST request just for the alert.
  // For demo, you might just print it to serial.
  Serial.println("Firebase Alert: Lat " + alertLat + ", Lon " + alertLon);

  // Keep alert message on display for a few seconds
  delay(5000);
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Mode: " + currentOperatingMode); // Return to normal display
  display.display();
}

void loop() {
  unsigned long currentMillis = millis();
  if (DEBUG_GPS) {
    // Simulate GPS data updates over time
    // This provides a continuous stream of dummy NMEA data to TinyGPS++
    // for demonstration purposes.
    static unsigned long lastDummyGPSTime = 0;
    static double currentDummyLat = 28.6139; // Start near Delhi
    static double currentDummyLon = 77.2090;

    if (millis() - lastDummyGPSTime > 1000) { // Update dummy GPS every 1 second
      lastDummyGPSTime = millis();

      // Simulate slight movement for distance calculation demo
      currentDummyLat += 0.0001; // Increase latitude slightly
      currentDummyLon += 0.0001; // Increase longitude slightly

      // Construct a dummy NMEA GPRMC sentence
      // This is simplified. For full realism, you'd calculate checksum, etc.
      // But TinyGPS++ is robust enough for basic valid sentences.
      // Format: $GPRMC,HHMMSS.sss,A,latitude,N,longitude,E,speed,course,DDMMYY,variation,E*CS<CR><LF>
      // Using a fixed time/date for simplicity in dummy data.
      // Construct the base NMEA GPRMC sentence without checksum yet
      // Example: $GPRMC,HHMMSS.sss,A,latitude,N,longitude,E,speed,course,DDMMYY,variation,E*CS<CR><LF>
      // Note: latitude/longitude for GPRMC are in ddmm.mmmm format, so we multiply by 100
      // E.g., 28.6139 becomes 2861.3900
      char baseSentence[80]; // Buffer for the part before checksum
      sprintf(baseSentence, "GPRMC,123456.00,A,%.4f,N,%.4f,E,15.0,120.0,010125,,", currentDummyLat * 100, currentDummyLon * 100);

      // Calculate checksum for the base sentence
      byte checksum = calculateNMEAChecksum(baseSentence);

      // Now assemble the full NMEA sentence with checksum
      char nmeaBuffer[100];
      sprintf(nmeaBuffer, "$%s*%02X\r\n", baseSentence, checksum); // Format: $SENTENCE*CSUM\r\n

      // Feed characters one by one to TinyGPS++
      // ... (the for loop and Serial.println("Dummy GPS data fed."); remains the same) ...

      // Feed characters one by one to TinyGPS++
      for (int i = 0; i < strlen(nmeaBuffer); i++) {
        gps.encode(nmeaBuffer[i]);
      }
      Serial.println("Dummy GPS data fed.");
      Serial.print("GPS Location Valid (after encode): ");
      Serial.println(gps.location.isValid() ? "TRUE" : "FALSE"); // Check if location is valid
      Serial.print("GPS Time Valid (after encode): ");
      Serial.println(gps.time.isValid() ? "TRUE" : "FALSE"); // Check if time is valid
    }
  } else {
    // Real GPS data from Neo-6M (when DEBUG_GPS is false)
    while (gpsSerial.available()) {
      gps.encode(gpsSerial.read());
    }
  }
  static int lastButtonReadState = HIGH; // Static variable to hold the raw reading from the pin (HIGH when not pressed for INPUT_PULLUP)
  static unsigned long lastDebounceTime = 0; // Static variable to store the last time the input pin was toggled
  static bool debouncedButtonState = HIGH; // Static variable to hold the actual, debounced state of the button (HIGH=released, LOW=pressed)

  int currentReading = digitalRead(ONBOARD_BUTTON_PIN); // Read the current raw state of the button pin

  // If the raw button state has changed (i.e., if the current reading is different from the last reading)
  if (currentReading != lastButtonReadState) {
    lastDebounceTime = millis(); // Reset the debouncing timer
  }

  // After a debounce delay, if the reading is stable and different from the last debounced state
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (currentReading != debouncedButtonState) { // If the stable current reading is different from our stored debounced state
      debouncedButtonState = currentReading; // Update the debounced state to the current stable reading

      // Only trigger toggle if the button is *pressed* (i.e., goes from HIGH to LOW)
      if (debouncedButtonState == LOW) { // If the confirmed stable state is LOW (button pressed)
        toggleTrackerState(); // Call the function to toggle the tracker's active state
        Serial.println("Button detected (Debounced)!"); // For debugging confirmation
      }
    }
  }
  lastButtonReadState = currentReading; // Save the current raw reading for the next iteration's comparison

  if (trackerActive) {

    if (gps.location.isValid() && gps.location.isUpdated()) {
      Serial.print("Lat: ");
      Serial.println(gps.location.lat(), 6);
      Serial.print("Lon: ");
      Serial.println(gps.location.lng(), 6);
      Serial.print("Speed: ");
      Serial.println(gps.speed.kmph());
      Serial.print("Satellites: ");
      Serial.println(gps.satellites.value());
      Serial.print("HDOP: ");
      Serial.println(gps.hdop.value() / 100.0);

      if (lastLat != 0.0 || lastLon != 0.0) {
        double segmentDistance = gps.distanceBetween(
            lastLat, lastLon, gps.location.lat(), gps.location.lng());

        if (gps.speed.kmph() > 0.5) {
          totalDistanceMeters += segmentDistance;
        }
      }

      lastLat = gps.location.lat();
      lastLon = gps.location.lng();
    } else {
      Serial.println("Waiting for valid GPS fix or new data...");
    }

    if ((currentMillis - lastDisplayUpdateMillis) >=
        DISPLAY_UPDATE_INTERVAL_MS) {
          Serial.println("Calling displayInfo()...");
      displayInfo();
      lastDisplayUpdateMillis = currentMillis;
    }

    if ((currentMillis - lastDataSendMillis) >= DATA_SEND_INTERVAL_MS) {
      if (gps.location.isValid()) {
        sendDataToServer();
      } else {
        Serial.println("Skipping data send: No valid GPS fix yet.");
      }
      lastDataSendMillis = currentMillis;
    }

  } else {

    if ((currentMillis - lastDisplayUpdateMillis) >=
        DISPLAY_UPDATE_INTERVAL_MS) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("TRACKER PAUSED");
      display.println("");
      display.print("Total Dist: " + String(totalDistanceMeters / 1000.0, 2) +
                    " km");
      display.println("");
      display.println("Press button to START");
      display.display();
      lastDisplayUpdateMillis = currentMillis;
    }
  }

  modem.maintain();
  delay(10);
}

void displayInfo() {
  display.clearDisplay();
  display.setCursor(0,0);

  // Line 1: Status/Mode
  display.print("Mode: "); // Changed from "Status:"
  display.println(currentOperatingMode); // Now prints the actual mode name (or "PAUSED")

  // ... rest of your displayInfo() function remains the same ...

  // Time
  if (gps.time.isValid() && gps.date.isValid()) {
    display.print("Time: ");
    if (gps.date.day() < 10) display.print("0"); display.print(gps.date.day()); display.print("/");
    if (gps.date.month() < 10) display.print("0"); display.print(gps.date.month()); display.print("/");
    display.print(gps.date.year()); // Full year
    display.println(); // Move to next line for time
    display.print("     "); // Indent for time
    if (gps.time.hour() < 10) display.print("0"); display.print(gps.time.hour()); display.print(":");
    if (gps.time.minute() < 10) display.print("0"); display.print(gps.time.minute()); display.print(":");
    if (gps.time.second() < 10) display.print("0"); display.print(gps.time.second());
    display.println();
  } else {
    display.println("Time: NO FIX");
    display.println("            "); // Blank line for spacing
  }

  // Line 4-5: Location & Speed
  if (gps.location.isValid()) {
    display.print("Lat: "); display.println(gps.location.lat(), 4);
    display.print("Lon: "); display.println(gps.location.lng(), 4);
    display.print("Spd: "); display.print(gps.speed.kmph(), 1); display.println(" km/h");
  } else {
    display.println("Loc: NO FIX");
    display.println("Spd: --.-- km/h");
  }

  // Line 6: Distance
  display.print("Dist: ");
  display.print(totalDistanceMeters / 1000.0, 2); // Convert meters to kilometers, display with 2 decimal places
  display.println(" km");

  display.display(); // Update the physical display with all the content
}

void sendDataToServer() {
  Serial.print("Sending data to server (Journey ID: ");
  Serial.print(journeyID);
  Serial.println(")...");

  if (DEBUG_GPRS) { // If in DEBUG_GPRS mode, only simulate locally
    Serial.println("DEBUG_GPRS is true: Simulating data transmission success locally.");
    // We are NOT actually sending via HTTPClient or the modem in this mode.
    // This section is just for confirming that 'sendDataToServer' is called
    // and for printing the payload it *would* send.
    
    // Construct the JSON payload for local display/logging
    String postData = "{\"journey_id\":" + String(journeyID) +
                      ",\"latitude\":" + String(gps.location.lat(), 6) +
                      ",\"longitude\":" + String(gps.location.lng(), 6) +
                      ",\"speed_kmph\":" + String(gps.speed.kmph(), 2) +
                      ",\"total_distance_meters\":" + String(totalDistanceMeters, 2) +
                      ",\"timestamp\":\"";
    if (gps.date.isValid() && gps.time.isValid()) {
      char timestampBuffer[25];
      sprintf(timestampBuffer, "%04d-%02d-%02dT%02d:%02d:%02dZ",
              gps.date.year(), gps.date.month(), gps.date.day(),
              gps.time.hour(), gps.time.minute(), gps.time.second());
      postData += String(timestampBuffer);
    } else {
      postData += "UNKNOWN_TIME_OR_NO_GPS_FIX";
    }
    postData += "\"}";
    
    Serial.print("Simulated Payload: "); Serial.println(postData);
    delay(500); // Simulate some processing time before printing complete
    Serial.println("Simulated local data send complete!");
    return; // EXIT THE FUNCTION HERE. Do not proceed to real HTTPClient calls.
  }

  // --- This block runs ONLY if DEBUG_GPRS is false (for real GPRS communication) ---
  Serial.println("DEBUG_GPRS is false: Attempting real GPRS communication.");

  // Check GPRS connection and reconnect if necessary
  if (!modem.isGprsConnected()) {
    Serial.print("Connecting to GPRS network (APN: "); Serial.print(apn); Serial.println(")...");
    if (!modem.gprsConnect(apn, user, pass)) {
      Serial.println("GPRS connection failed!");
      return; // Exit if GPRS fails to connect
    }
    Serial.println("GPRS connected!");
  }

  // Prepare JSON payload for actual transmission (using real GPS data)
  HTTPClient http;
  String postData = "{\"journey_id\": " + String(journeyID) +
                    ",\"latitude\":" + String(gps.location.lat(), 6) +
                    ",\"longitude\":" + String(gps.location.lng(), 6) +
                    ",\"speed_kmph\":" + String(gps.speed.kmph(), 2) +
                    ",\"total_distance_meters\":" + String(totalDistanceMeters, 2) +
                    ",\"timestamp\":\"";
  if (gps.date.isValid() && gps.time.isValid()) {
    char timestampBuffer[25];
    sprintf(timestampBuffer, "%04d-%02d-%02dT%02d:%02d:%02dZ",
            gps.date.year(), gps.date.month(), gps.date.day(),
            gps.time.hour(), gps.time.minute(), gps.time.second());
    postData += String(timestampBuffer);
  } else {
    postData += "UNKNOWN_TIME_OR_NO_GPS_FIX";
  }
  postData += "\"}";

  // Perform the actual HTTP POST request
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }
  http.end(); // Close the connection
}

void toggleTrackerState() {
  Serial.print("Button Pressed. Current Mode: ");
  Serial.println(currentOperatingMode);

  if (currentOperatingMode == "WORKOUT") {
    // --- WORKOUT Mode: Button Toggles PAUSE/RESUME ---
    trackerActive = !trackerActive; // Flip the active state
    if (trackerActive) {
      Serial.println("WORKOUT Tracker ACTIVATED!");
      totalDistanceMeters = 0.0; // Reset distance for new workout session
      lastLat = 0.0;
      lastLon = 0.0;
      generateNewJourneyID(); // New ID for new workout
      Serial.print("New Journey ID: "); Serial.println(journeyID);
      display.clearDisplay(); display.setCursor(0,0);
      display.println("WORKOUT START!");
      display.display();
    } else {
      Serial.println("WORKOUT Tracker PAUSED!");
      display.clearDisplay(); display.setCursor(0,0);
      display.println("WORKOUT PAUSED");
      display.display();
    }
  } else if (currentOperatingMode == "PERSONAL SAFETY") {
    // --- PERSONAL SAFETY Mode: Button Triggers ALERT ---
    triggerAlert(); // Call a new function to handle the alert sequence
  } else if (currentOperatingMode == "ASSET TRACKING") {
    // --- ASSET TRACKING Mode: Button Toggles START/STOP ---
    trackerActive = !trackerActive; // Flip the active state
    if (trackerActive) {
      Serial.println("ASSET TRACKING ACTIVATED!");
      totalDistanceMeters = 0.0; // Reset distance for new asset tracking session
      lastLat = 0.0;
      lastLon = 0.0;
      generateNewJourneyID(); // New ID for new tracking
      Serial.print("New Journey ID: "); Serial.println(journeyID);
      display.clearDisplay(); display.setCursor(0,0);
      display.println("ASSET TRACK START!");
      display.display();
    } else {
      Serial.println("ASSET TRACKING PAUSED!");
      display.clearDisplay(); display.setCursor(0,0);
      display.println("ASSET TRACK PAUSED");
      display.display();
    }
  } else {
    // Default or unknown mode
    Serial.println("Unknown Mode. Button has no action.");
    display.clearDisplay(); display.setCursor(0,0);
    display.println("Unknown Mode");
    display.display();
  }
  lastDisplayUpdateMillis = millis(); // Force immediate display update
}

void generateNewJourneyID() {
  // This generates a very basic ID based on current milliseconds and a random number.
  // For a robust system, especially if multiple trackers might use the same backend,
  // consider a UUID, or getting an ID assigned by your backend server.
  randomSeed(millis()); // Seed the random number generator using current time
  journeyID = millis() + random(1000000); // Add a larger random number for uniqueness
}

byte calculateNMEAChecksum(const char *sentence) {
    byte checksum = 0;
    // All characters between $ and * (exclusive)
    int i = 0;
    if (sentence[i] == '$') { // Skip the '$'
        i++;
    }
    while (sentence[i] != '*' && sentence[i] != '\0') {
        checksum ^= sentence[i]; // XOR each character
        i++;
    }
    return checksum;
}