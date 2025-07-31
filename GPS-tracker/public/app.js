// --- Firebase Initialization (Ensure this is your correct config) ---
const firebaseConfig = {
  apiKey: "AIzaSyDSauIYdrgdpHV2eDqo4UILnwhLk3oVIgk",

  authDomain: "gps-tracker-59cb6.firebaseapp.com",

  databaseURL: "https://gps-tracker-59cb6-default-rtdb.firebaseio.com",

  projectId: "gps-tracker-59cb6",

  storageBucket: "gps-tracker-59cb6.firebasestorage.app",

  messagingSenderId: "865039696235",

  appId: "1:865039696235:web:411f8fdadc441ab9210c2e",

  measurementId: "G-YL52K24439",
};

// Initialize Firebase
firebase.initializeApp(firebaseConfig);
const db = firebase.database();
const rtdb = firebase.database();

// --- UI Element References ---
const lastUpdatedSpan = document.getElementById("last-updated");
const latitudeSpan = document.getElementById("latitude");
const longitudeSpan = document.getElementById("longitude");
const speedSpan = document.getElementById("speed");
const journeyListDiv = document.getElementById("journey-list");
const currentSelectedJourneyIdDisplay = document.getElementById(
  "current-journey-id-display"
);

// Mode Switching UI
const modeSelect = document.getElementById("modeSelect");
const setModeButton = document.getElementById("setModeButton");
const lastSetModeDisplay = document.getElementById("lastSetMode");
const deviceIdDisplay = document.getElementById("deviceIdDisplay");

// --- Map Initialization Variables ---
let map;
let liveMarker;
let currentJourneyPolyline;
let lastKnownLat = 20.5937; // Default to center of India
let lastKnownLon = 78.9629;
const defaultZoom = 5;

// --- Global State ---
let activeJourneyId = null;
let liveLocationUnsubscribe = null;
let deviceStatusUnsubscribe = null; // To stop listening to old device status

// --- Functions ---

function initializeMap() {
  map = L.map("map-container").setView(
    [lastKnownLat, lastKnownLon],
    defaultZoom
  );
  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    attribution:
      '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  }).addTo(map);

  liveMarker = L.marker([lastKnownLat, lastKnownLon])
    .addTo(map)
    .bindPopup("Live Tracker")
    .openPopup();
  currentJourneyPolyline = L.polyline([], { color: "blue", weight: 3 }).addTo(
    map
  );
}

function updateLiveDisplay(lat, lon, speed, timestamp) {
  lastKnownLat = lat;
  lastKnownLon = lon;

  liveMarker.setLatLng([lat, lon]);
  map.panTo([lat, lon], { animate: true, duration: 1.0 });

  lastUpdatedSpan.textContent = timestamp
    ? new Date(timestamp.seconds * 1000).toLocaleTimeString()
    : "N/A";
  latitudeSpan.textContent = lat.toFixed(6);
  longitudeSpan.textContent = lon.toFixed(6);
  speedSpan.textContent = (speed || 0).toFixed(2) + " km/h";
}

// THIS IS THE NEW DYNAMIC FUNCTION FOR MODE CONTROL
function setupDeviceListeners(deviceId) {
  if (!deviceId) return;

  activeJourneyId = deviceId;
  currentSelectedJourneyIdDisplay.textContent = deviceId;
  deviceIdDisplay.textContent = deviceId; // Update the display for mode control

  // Stop listening to the old device status
  if (deviceStatusUnsubscribe) {
    deviceStatusUnsubscribe();
  }

  const deviceConfigRef = rtdb.ref(`devices/${deviceId}/config`);
  const deviceStatusRef = rtdb.ref(`devices/${deviceId}/status`);

  // Set up the listener for the mode control button
  setModeButton.onclick = () => {
    const selectedMode = modeSelect.value;
    console.log(`Setting mode for device ${deviceId} to: ${selectedMode}`);
    deviceConfigRef
      .update({
        mode: selectedMode,
        timestamp: firebase.database.ServerValue.TIMESTAMP,
      })
      .then(() =>
        alert(`Mode set to "${selectedMode}" for device ${deviceId}.`)
      )
      .catch((error) => console.error("Error setting mode:", error));
  };

  // Listen for status updates from the currently active device
  deviceStatusUnsubscribe = deviceStatusRef.on("value", (snapshot) => {
    const statusData = snapshot.val();
    if (statusData && statusData.currentMode) {
      lastSetModeDisplay.textContent = statusData.currentMode;
    }
  });
}

function setupLiveLocationListener(journeyId) {
  if (liveLocationUnsubscribe) {
    liveLocationUnsubscribe();
  }

  // Set up the mode controls to target this new, active journey
  setupDeviceListeners(journeyId);

  console.log("Now listening for live data from Journey ID:", journeyId);

  const journeyRef = rtdb.ref(`journeys/${journeyId}/points`);
  liveLocationUnsubscribe = journeyRef
    .orderByChild("receivedAt")
    .limitToLast(1)
    .on(
      "child_added",
      (snapshot) => {
        const latestPoint = snapshot.val();
        if (latestPoint) {
          updateLiveDisplay(
            latestPoint.latitude,
            latestPoint.longitude,
            latestPoint.speed_kmph,
            { seconds: Math.floor(latestPoint.receivedAt / 1000) } // Convert to Firestore-like timestamp
          );
        }
      },
      (error) => {
        console.error("Error getting live GPS point:", error);
      }
    );
}

function setupJourneyListListener() {
  const journeysRef = rtdb.ref("journeys");
  journeysRef
    .orderByChild("last_updated_server")
    .limitToLast(10)
    .on(
      "value",
      (snapshot) => {
        journeyListDiv.innerHTML = "";
        if (!snapshot.exists()) {
          journeyListDiv.innerHTML = "<p>No journeys recorded yet.</p>";
          return;
        }

        const journeys = [];
        snapshot.forEach((childSnapshot) => {
          journeys.unshift({ id: childSnapshot.key, ...childSnapshot.val() }); // Add to beginning to sort descending
        });

        const latestJourneyId = journeys[0].id;
        if (latestJourneyId !== activeJourneyId) {
          setupLiveLocationListener(latestJourneyId);
        }

        journeys.forEach((journey) => {
          const journeyId = journey.id;
          const journeyLi = document.createElement("li");
          journeyLi.className = "journey-item";
          journeyLi.textContent = `ID: ${journeyId}`;
          // ... (rest of the styling)
          journeyListDiv.appendChild(journeyLi);
        });
      },
      (error) => {
        console.error("Error setting up journey list listener:", error);
      }
    );
}

// --- Initialize the dashboard when the page loads ---
document.addEventListener("DOMContentLoaded", () => {
  initializeMap();
  setupJourneyListListener();
});
