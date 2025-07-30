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
const db = firebase.firestore();
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
let liveLocationUnsubscribe = null; // To stop listening to old journeys

// --- Functions ---

// Initializes the Leaflet map
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

// Updates the live data display and map marker
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

// Sets up a listener for live location updates for a specific journey ID
function setupLiveLocationListener(journeyId) {
  if (liveLocationUnsubscribe) {
    liveLocationUnsubscribe(); // Stop listening to the previous journey
  }

  activeJourneyId = journeyId;
  currentSelectedJourneyIdDisplay.textContent = journeyId;
  console.log("Now listening for live data from Journey ID:", journeyId);

  liveLocationUnsubscribe = db
    .collection("journeys")
    .doc(journeyId)
    .collection("points")
    .orderBy("receivedAt", "desc")
    .limit(1)
    .onSnapshot(
      (snapshot) => {
        if (!snapshot.empty) {
          const latestPoint = snapshot.docs[0].data();
          console.log("New live data received:", latestPoint);
          updateLiveDisplay(
            latestPoint.latitude,
            latestPoint.longitude,
            latestPoint.speed_kmph,
            latestPoint.receivedAt
          );
        }
      },
      (error) => {
        console.error("Error getting live GPS point:", error);
      }
    );
}

// Listens for changes in the 'journeys' collection to update the sidebar list
function setupJourneyListListener() {
  db.collection("journeys")
    .orderBy("last_updated_server", "desc")
    .limit(10) // Get the 10 most recent journeys
    .onSnapshot(
      (snapshot) => {
        journeyListDiv.innerHTML = ""; // Clear the list
        if (snapshot.empty) {
          journeyListDiv.innerHTML = "<p>No journeys recorded yet.</p>";
          return;
        }

        // --- AUTOMATICALLY LISTEN TO THE NEWEST JOURNEY ---
        const latestJourneyId = snapshot.docs[0].id;
        if (latestJourneyId !== activeJourneyId) {
          setupLiveLocationListener(latestJourneyId);
        }
        // ----------------------------------------------------

        snapshot.forEach((doc) => {
          const journeyId = doc.id;
          const journeyLi = document.createElement("li");
          journeyLi.className = "journey-item";
          journeyLi.textContent = `ID: ${journeyId}`;
          journeyLi.style.cursor = "pointer";
          journeyLi.style.padding = "8px 0";
          journeyLi.style.borderBottom = "1px dotted #ccc";

          // Highlight the currently active journey
          if (journeyId === activeJourneyId) {
            journeyLi.style.fontWeight = "bold";
            journeyLi.style.backgroundColor = "#e0e0e0";
          }

          journeyListDiv.appendChild(journeyLi);
        });
      },
      (error) => {
        console.error("Error setting up journey list listener:", error);
      }
    );
}

// --- Mode Switching Logic ---
const deviceConfigRef = rtdb.ref("devices/" + "12345" + "/config");
const deviceStatusRef = rtdb.ref("devices/" + "12345" + "/status");

if (setModeButton) {
  deviceIdDisplay.textContent = "12345";
  setModeButton.addEventListener("click", () => {
    const selectedMode = modeSelect.value;
    console.log(`Setting mode for device to: ${selectedMode}`);

    deviceConfigRef
      .update({
        mode: selectedMode,
        timestamp: firebase.database.ServerValue.TIMESTAMP,
      })
      .then(() => alert(`Mode set to "${selectedMode}"`))
      .catch((error) => console.error("Error setting mode:", error));
  });
}

// Listen for the device's reported status
deviceStatusRef.on(
  "value",
  (snapshot) => {
    const statusData = snapshot.val();
    if (statusData && statusData.currentMode) {
      lastSetModeDisplay.textContent = statusData.currentMode;
    }
  },
  (error) => console.error("Error listening to device status:", error)
);

// --- Initialize the dashboard when the page loads ---
document.addEventListener("DOMContentLoaded", () => {
  initializeMap();
  setupJourneyListListener();
});
