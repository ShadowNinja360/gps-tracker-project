#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGsmClient.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <WiFi.h> // Required for Firebase library
#include <FirebaseESP32.h>

// --- Pin Definitions ---
#define OLED_RESET -1
#define RXD2 16
#define TXD2 17
#define BUILTIN_BUTTON 0
#define EXTERNAL_BUTTON 15

// --- GPRS Credentials ---
const char apn[] = "airtelgprs.com"; // Your GPRS APN
const char gprsUser[] = "";
const char gprsPass[] = "";

// --- Firebase Configuration ---
#define FIREBASE_HOST "https://gps-tracker-59cb6-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "YOUR_DATABASE_SECRET" // Your RTDB secret

// --- Server Configuration ---
const char server[] = "us-central1-gps-tracker-59cb6.cloudfunctions.net";
const String resource = "/receiveGPSData";

// --- Device ID ---
#define DEVICE_ID "12345"

// --- Global Objects ---
SoftwareSerial sim800lSerial(SIM800L_RX_PIN, SIM800L_TX_PIN);
TinyGsm modem(sim800lSerial);
TinyGsmClient client(modem);
TinyGPSPlus gps;
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);
FirebaseData fbdo;
FirebaseData stream;

// --- Global State Variables ---
enum DeviceMode {
    WORKOUT,
    PERSONAL_SAFETY,
    ASSET_TRACKING
};
volatile DeviceMode currentMode = WORKOUT;
volatile bool modeChanged = false;
String modeNames[] = {"WORKOUT", "PERSONAL SAFETY", "ASSET TRACKING"};

bool trackerActive = false;
double totalDistanceMeters = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;
unsigned long journeyID = 0;
unsigned long lastDataSendMillis = 0;
// No longer a single constant for the interval

// --- Function Prototypes ---
void setupGsm();
void setupFirebase();
void streamCallback(StreamData data);
void streamTimeoutCallback(bool timeout);
void sendDataToServer();
void updateDisplay(String line1, String line2 = "", String line3 = "", String line4 = "");
void reportStatusToFirebase();
void IRAM_ATTR handleBuiltinButton();
void handleExternalButton();
DeviceMode stringToMode(String modeStr);
void generateNewJourneyID();
void toggleTrackerState();


void setup() {
    Serial.begin(115200);

    pinMode(BUILTIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUILTIN_BUTTON), handleBuiltinButton, FALLING);
    pinMode(EXTERNAL_BUTTON, INPUT_PULLUP);

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

    updateDisplay("Ready!", "Press button to toggle", "Start/Stop");
}

void loop() {
    unsigned long currentMillis = millis();

    if (Firebase.ready() && !stream.getStreamPath().length()) {
        String streamPath = "/devices/" + String(DEVICE_ID) + "/config";
        if (!Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback)) {
            Serial.println("Failed to set stream callback");
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

    if (digitalRead(BUILTIN_BUTTON) == LOW) {
        static unsigned long lastPress = 0;
        if (currentMillis - lastPress > 500) { // Simple debounce
            toggleTrackerState();
            lastPress = currentMillis;
        }
    }

    if (trackerActive) {
        if (gps.location.isValid() && gps.location.isUpdated()) {
            if (lastLat != 0.0 || lastLon != 0.0) {
                totalDistanceMeters += gps.distanceBetween(lastLat, lastLon, gps.location.lat(), gps.location.lng());
            }
            lastLat = gps.location.lat();
            lastLon = gps.location.lng();
        }

        // *** DYNAMIC INTERVAL LOGIC ***
        unsigned long currentSendInterval;
        if (currentMode == WORKOUT) {
            currentSendInterval = 10000; // 10 seconds
        } else {
            currentSendInterval = 30000; // 30 seconds for Personal Safety and Asset Tracking
        }

        if (currentMillis - lastDataSendMillis >= currentSendInterval) {
            if (gps.location.isValid()) {
                sendDataToServer();
            } else {
                Serial.println("Skipping data send: No valid GPS fix.");
                updateDisplay("Status: ACTIVE", "No GPS Signal...");
            }
            lastDataSendMillis = currentMillis;
        }
    }
}

void toggleTrackerState() {
    trackerActive = !trackerActive;
    if (trackerActive) {
        Serial.println("Tracker ACTIVATED!");
        updateDisplay("Tracker: ON", "Mode: " + modeNames[(int)currentMode]);
        totalDistanceMeters = 0.0;
        lastLat = 0.0;
        lastLon = 0.0;
        generateNewJourneyID();
    } else {
        Serial.println("Tracker PAUSED!");
        updateDisplay("Tracker: OFF");
    }
}


void setupGsm() {
    updateDisplay("Initializing GSM...");
    sim800lSerial.begin(SIM800L_BAUDRATE, SERIAL_8N1, SIM800L_RX_PIN, SIM800L_TX_PIN);
    delay(3000);

    Serial.println("Initializing modem...");
    if (!modem.init()) {
        updateDisplay("Modem Init Failed");
        while (1);
    }

    updateDisplay("Waiting for Network...");
    if (!modem.waitForNetwork(60000L)) {
        updateDisplay("Network Failed");
        while (1);
    }

    updateDisplay("Connecting to GPRS...");
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
        updateDisplay("GPRS Failed");
        while (1);
    }
    updateDisplay("GPRS Connected");
}

void setupFirebase() {
    updateDisplay("Connecting to", "Firebase...");
    Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
    Firebase.reconnectWiFi(true);
    Firebase.set początkowy(fbdo, "/devices/" + String(DEVICE_ID) + "/status");
}

void streamCallback(StreamData data) {
    if (data.dataType() == "json") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData jsonData;
        if (json.get(jsonData, "mode")) {
            String newModeStr = jsonData.stringValue;
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
        Serial.println("Server connection failed!");
        updateDisplay("Error:", "Server Connect Fail");
        return;
    }

    String postData = "{\"journey_id\": " + String(journeyID) +
                      ",\"latitude\":" + String(lastLat, 6) +
                      ",\"longitude\":" + String(lastLon, 6) +
                      ",\"speed_kmph\":" + String(gps.speed.kmph(), 2) +
                      ",\"total_distance_meters\":" + String(totalDistanceMeters, 2) + "}";

    String request = "POST " + resource + " HTTP/1.1\r\n";
    request += "Host: " + String(server) + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(postData.length()) + "\r\n";
    request += "\r\n";
    request += postData;

    client.print(request);

    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 5000L) {
        while (client.available()) {
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

void IRAM_ATTR handleBuiltinButton() {
    static unsigned long last_interrupt_time = 0;
    unsigned long interrupt_time = millis();
    if (interrupt_time - last_interrupt_time > 200) { // Debounce
        currentMode = (DeviceMode)(((int)currentMode + 1) % 3);
        modeChanged = true;
    }
    last_interrupt_time = interrupt_time;
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