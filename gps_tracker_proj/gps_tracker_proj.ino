// --- Library Includes ---
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <WiFi.h>

// --- IMPORTANT: Define the modem model BEFORE including TinyGSM ---
#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h>
#include <TinyGPS++.h>
#include "Firebase_ESP_Client.h"

// --- Pin Definitions & Serial Ports ---
#define OLED_RESET -1
#define BUILTIN_BUTTON 0
#define EXTERNAL_BUTTON 15

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
HardwareSerial gpsSerial(2);

#define SIM800L_RX_PIN 26
#define SIM800L_TX_PIN 27
HardwareSerial sim800lSerial(1);

// --- GPRS Credentials ---
const char apn[] = "airtelgprs.com"; // Your GPRS APN
const char gprsUser[] = "";
const char gprsPass[] = "";

// --- Firebase Configuration ---
#define API_KEY "YOUR_FIREBASE_API_KEY" // Your Firebase Web API Key
#define DATABASE_URL "https://gps-tracker-59cb6-default-rtdb.firebaseio.com"

// --- Server Configuration ---
const char server[] = "us-central1-gps-tracker-59cb6.cloudfunctions.net";
const String resource = "/receiveGPSData";

// --- Device ID ---
#define DEVICE_ID "12345"

// --- Global Objects ---
TinyGsm modem(sim800lSerial);
TinyGsmClient client(modem);
TinyGPSPlus gps;
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

// Firebase Objects
FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

// --- Global State Variables ---
enum DeviceMode { WORKOUT, PERSONAL_SAFETY, ASSET_TRACKING };
volatile DeviceMode currentMode = WORKOUT;
volatile bool modeChanged = false;
volatile bool buttonPressed = false; // Flag for the interrupt
String modeNames[] = {"WORKOUT", "PERSONAL SAFETY", "ASSET TRACKING"};
bool trackerActive = false;
double totalDistanceMeters = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;
unsigned long journeyID = 0;
unsigned long lastDataSendMillis = 0;

// --- Function Prototypes (Forward Declaration) ---
void streamCallback(FirebaseStream data);
void streamTimeoutCallback(bool timeout);
void IRAM_ATTR handleBuiltinButton();
void toggleTrackerState();
void setupGsm();
void setupFirebase();
void sendDataToServer();
void updateDisplay(String line1, String line2 = "", String line3 = "", String line4 = "");
void reportStatusToFirebase();
void handleExternalButton();
DeviceMode stringToMode(String modeStr);
void generateNewJourneyID();


void setup() {
    Serial.begin(115200);
    pinMode(BUILTIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUILTIN_BUTTON), handleBuiltinButton, FALLING);
    pinMode(EXTERNAL_BUTTON, INPUT_PULLUP);

    Wire.begin();
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    updateDisplay("System Booting...");
    delay(1000);

    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    updateDisplay("Initializing GPS...");
    delay(1000);

    setupGsm();
    setupFirebase();
    generateNewJourneyID();
    updateDisplay("Ready!", "Press button to", "Start/Stop");
}

void loop() {
    unsigned long currentMillis = millis();

    // Check if the interrupt has set the button press flag
    if (buttonPressed) {
        toggleTrackerState(); // Call the function to change the tracker's state
        buttonPressed = false; // Reset the flag immediately
    }

    if (Firebase.ready() && !stream.streamPath().length()) {
        String streamPath = "/devices/" + String(DEVICE_ID) + "/config";
        if (!Firebase.RTDB.beginStream(&stream, streamPath)) {
            Serial.printf("Error beginning stream: %s\n", stream.errorReason().c_str());
        } else {
            Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
        }
    }

    if (modeChanged) {
        updateDisplay("Mode Changed:", modeNames[(int)currentMode]);
        reportStatusToFirebase();
        delay(1500);
        modeChanged = false;
    }

    handleExternalButton();

    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (trackerActive) {
        if (gps.location.isUpdated() && gps.location.isValid()) {
             if (lastLat != 0.0 || lastLon != 0.0) {
                totalDistanceMeters += TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), lastLat, lastLon);
            }
            lastLat = gps.location.lat();
            lastLon = gps.location.lng();
        }

        unsigned long currentSendInterval = (currentMode == WORKOUT) ? 10000 : 30000;

        if (currentMillis - lastDataSendMillis >= currentSendInterval) {
            if (gps.location.isValid()) {
                sendDataToServer();
            } else {
                updateDisplay("Status: ACTIVE", "No GPS Signal...");
            }
            lastDataSendMillis = currentMillis;
        }
    }
}

void IRAM_ATTR handleBuiltinButton() {
    static unsigned long last_interrupt_time = 0;
    unsigned long interrupt_time = millis();
    if (interrupt_time - last_interrupt_time > 500) { // Debounce for 500ms
        buttonPressed = true; // Set the flag for the main loop to handle
    }
    last_interrupt_time = interrupt_time;
}

void toggleTrackerState() {
    trackerActive = !trackerActive;
    if (trackerActive) {
        totalDistanceMeters = 0.0;
        lastLat = 0.0;
        lastLon = 0.0;
        generateNewJourneyID();
        updateDisplay("Tracker: ON", "Mode: " + modeNames[(int)currentMode]);
    } else {
        updateDisplay("Tracker: OFF");
    }
}

void setupGsm() {
    updateDisplay("Initializing GSM...");
    sim800lSerial.begin(9600, SERIAL_8N1, SIM800L_RX_PIN, SIM800L_TX_PIN);
    delay(3000);
    
    Serial.println("Initializing modem...");
    if (!modem.init()) {
        updateDisplay("Modem Init Failed"); while (1);
    }
    
    // --- ADD THIS LINE ---
    delay(5000); // Give the modem 5 seconds to boot and scan for networks
    // ---------------------

    updateDisplay("Waiting for Network...");
    if (!modem.waitForNetwork(60000L)) {
        updateDisplay("Network Failed"); while (1);
    }
    // ... rest of the function remains the same
}

void setupFirebase() {
    updateDisplay("Connecting to", "Firebase...");
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    // Sign in anonymously
    auth.user.email = "";
    auth.user.password = "";

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
}

void streamCallback(FirebaseStream data) {
    if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json) {
        FirebaseJson *json = data.to<FirebaseJson *>();
        FirebaseJsonData jsonData;
        if (json->get(jsonData, "mode")) {
            String newModeStr = jsonData.to<String>();
            DeviceMode newMode = stringToMode(newModeStr);
            if (newMode != currentMode) {
                currentMode = newMode;
                modeChanged = true;
            }
        }
    }
}

void streamTimeoutCallback(bool timeout) {
    if (timeout) Serial.println("Stream timeout, resuming...");
}

void sendDataToServer() {
    updateDisplay("Sending Data...", "Lat: " + String(lastLat, 4), "Lon: " + String(lastLon, 4));
    if (!client.connect(server, 80)) {
        updateDisplay("Error:", "Server Connect Fail");
        return;
    }

    String postData = "{\"journey_id\": \"" + String(journeyID) +
                      "\",\"latitude\":" + String(lastLat, 6) +
                      ",\"longitude\":" + String(lastLon, 6) +
                      ",\"speed_kmph\":" + String(gps.speed.kmph(), 2) +
                      ",\"total_distance_meters\":" + String(totalDistanceMeters, 2) + "}";

    String request = "POST " + resource + " HTTP/1.1\r\n";
    request += "Host: " + String(server) + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(postData.length()) + "\r\n\r\n" + postData;
    
    client.print(request);
    
    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 5000L) {
        if (client.available()) {
            Serial.write(client.read());
        }
    }
    
    client.stop();
    updateDisplay("Data Sent!", "Mode: " + modeNames[(int)currentMode]);
}

void updateDisplay(String line1, String line2, String line3, String line4) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(line1);
    display.println(line2);
    display.println(line3);
    display.println(line4);
    display.display();
}

void reportStatusToFirebase() {
    if (Firebase.ready()) {
        String path = "/devices/" + String(DEVICE_ID) + "/status";
        FirebaseJson json;
        json.set("currentMode", modeNames[(int)currentMode]);
        if (!Firebase.RTDB.setJSON(&fbdo, path, &json)) {
            Serial.println("Failed to report status: " + fbdo.errorReason());
        }
    }
}

void handleExternalButton() {
    if (digitalRead(EXTERNAL_BUTTON) == LOW) {
        delay(50);
        if (digitalRead(EXTERNAL_BUTTON) == LOW) {
            String action = "Ext. Button Action\nMode: " + modeNames[(int)currentMode];
             if(currentMode == PERSONAL_SAFETY){
                action = "!!! SOS !!!\nSending Alert...";
             }
            updateDisplay(action);
            delay(2000);
            while(digitalRead(EXTERNAL_BUTTON) == LOW);
        }
    }
}

DeviceMode stringToMode(String modeStr) {
    if (modeStr == "workout") return WORKOUT;
    if (modeStr == "personal_safety") return PERSONAL_SAFETY;
    if (modeStr == "asset_tracking") return ASSET_TRACKING;
    return currentMode;
}

void generateNewJourneyID() {
  journeyID = millis() + random(100000);
  Serial.print("New Journey ID: ");
  Serial.println(journeyID);
}