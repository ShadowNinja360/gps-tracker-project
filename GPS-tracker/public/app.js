// --- Firebase Initialization (Paste your exact firebaseConfig here) ---
// Your web app's Firebase configuration
// This exact object MUST come from your Firebase Console -> Project settings -> Your apps -> Web app
const firebaseConfig = {
  apiKey: "AIzaSyDSauIYdrgdpHV2eDqo4UILnwhLk3oVIgk", // !!! REPLACE THIS WITH YOUR ACTUAL API KEY !!!
  authDomain: "gps-tracker-59cb6.firebaseapp.com",
  projectId: "gps-tracker-59cb6",
  storageBucket: "gps-tracker-59cb6.firebasestorage.app",
  messagingSenderId: "865039696235",
  appId: "1:865039696235:web:411f8fdadc441ab9210c2e",
  measurementId: "G-YL52K24439", // Optional, if you enabled Google Analytics
};

// Initialize Firebase
firebase.initializeApp(firebaseConfig);
const db = firebase.firestore(); // Get a reference to the Firestore database
const rtdb = firebase.database(); // Get a reference to the Realtime Database (for mode switching)

// --- Device ID Configuration (IMPORTANT: Must match your ESP32's journey_id for testing) ---
// For a prototype, you might hardcode a single test device ID or use a dynamic one later.
// If your ESP32 generates random journey IDs, you'll need to update this or list them dynamically.
const TEST_DEVICE_ID = "12345"; // !!! REPLACE THIS with a known journey_id from your ESP32 !!!

// --- UI Element References ---
const lastUpdatedSpan = document.getElementById("last-updated");
const latitudeSpan = document.getElementById("latitude");
const longitudeSpan = document.getElementById("longitude");
const speedSpan = document.getElementById("speed");
const journeyListDiv = document.getElementById("journey-list");
const currentSelectedJourneyIdDisplay = document.getElementById(
  "current-journey-id-display"
);
const startDemoModeButton = document.getElementById("startDemoModeButton");
const stopDemoModeButton = document.getElementById("stopDemoModeButton");

// Mode Switching UI
const modeSelect = document.getElementById("modeSelect");
const setModeButton = document.getElementById("setModeButton");
const lastSetModeDisplay = document.getElementById("lastSetMode");

// --- Map Initialization Variables ---
let map;
let liveMarker = null; // Initialize as null
let currentJourneyPolyline;
let lastKnownLat = 20.5937; // Default center of India
let lastKnownLon = 78.9629; // Default center of India
const defaultZoom = 5;

// --- Global State for Tracking ---
let activeJourneyId = null; // Stores the journey ID currently being displayed/tracked live

// --- Functions ---

// Initializes the Leaflet map
function initializeMap() {
  // Check if map-container exists before initializing map
  const mapContainer = document.getElementById("map-container");
  if (mapContainer) {
    map = L.map("map-container").setView(
      [lastKnownLat, lastKnownLon],
      defaultZoom
    );
    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    }).addTo(map);

    // Add initial live marker
    liveMarker = L.marker([lastKnownLat, lastKnownLon])
      .addTo(map)
      .bindPopup("Live Tracker")
      .openPopup();

    // Add polyline for current active journey
    currentJourneyPolyline = L.polyline([], { color: "blue", weight: 3 }).addTo(
      map
    );
  } else {
    console.error("Map container not found!");
  }
}

// Updates the live marker and polyline
function updateMapAndLiveDisplay(
  lat,
  lon,
  speed_kmph,
  total_distance_meters,
  timestamp
) {
  lastKnownLat = lat;
  lastKnownLon = lon;

  if (liveMarker) {
    liveMarker.setLatLng([lat, lon]);
    liveMarker.setPopupContent(
      `Lat: ${lat.toFixed(6)}<br>Lon: ${lon.toFixed(
        6
      )}<br>Speed: ${speed_kmph.toFixed(2)} km/h<br>Dist: ${(
        total_distance_meters / 1000
      ).toFixed(2)} km`
    );
  } else {
    // If marker doesn't exist for some reason, create it
    liveMarker = L.marker([lat, lon])
      .addTo(map)
      .bindPopup(`Lat: ${lat.toFixed(6)}<br>Lon: ${lon.toFixed(6)}`)
      .openPopup();
  }

  if (currentJourneyPolyline) {
    currentJourneyPolyline.addLatLng([lat, lon]);
    map.panTo([lat, lon]); // Keep map centered on current location
  }

  // Update text display
  lastUpdatedSpan.textContent = timestamp
    ? timestamp.toDate().toLocaleTimeString()
    : "N/A";
  latitudeSpan.textContent = lat.toFixed(6);
  longitudeSpan.textContent = lon.toFixed(6);
  speedSpan.textContent = speed_kmph.toFixed(2) + " km/h";
  // You might want a dedicated span for total distance if it's not already covered
  // e.g., document.getElementById('total-distance-span').textContent = (total_distance_meters / 1000).toFixed(2) + ' km';
}

// Fetches and displays a specific journey's full route
async function displayJourneyRoute(journeyId) {
  currentSelectedJourneyIdDisplay.textContent = journeyId; // Update displayed ID

  if (currentJourneyPolyline) {
    currentJourneyPolyline.setLatLngs([]); // Clear previous live route for this session
  }

  // Clear any previously loaded historical polylines
  map.eachLayer(function (layer) {
    if (layer instanceof L.Polyline && layer.options.color === "red") {
      // Check for historical polyline color
      map.removeLayer(layer);
    }
  });

  // Fetch all points for this journey from Firestore
  const snapshot = await db
    .collection("journeys")
    .doc(journeyId)
    .collection("points")
    .orderBy("timestamp", "asc") // Order by GPS timestamp
    .get();

  const latlngs = [];
  if (!snapshot.empty) {
    snapshot.forEach((doc) => {
      const data = doc.data();
      latlngs.push([data.latitude, data.longitude]);
    });
    if (latlngs.length > 0) {
      // Create a new polyline for the historical route
      const historicalPolyline = L.polyline(latlngs, {
        color: "red",
        weight: 3,
      }).addTo(map);
      map.fitBounds(historicalPolyline.getBounds()); // Zoom to fit the entire route

      // Make sure the live marker starts at the end of this historical route
      const lastPoint = latlngs[latlngs.length - 1];
      if (liveMarker) liveMarker.setLatLng(lastPoint);
      else liveMarker = L.marker(lastPoint).addTo(map);

      console.log(
        `Displayed historical route for ${journeyId} with ${latlngs.length} points.`
      );
    }
  } else {
    console.log("No points found for journey:", journeyId);
    alert("No route data available for this journey.");
  }
}

// Listens for changes in the main 'journeys' collection to update the sidebar list
function setupJourneyListListener() {
  db.collection("journeys")
    .orderBy("last_updated_server", "desc") // Order by when the server last received data
    .onSnapshot(
      (snapshot) => {
        journeyListDiv.innerHTML = ""; // Clear existing list
        if (snapshot.empty) {
          journeyListDiv.innerHTML = "<p>No journeys recorded yet.</p>";
          return;
        }

        snapshot.forEach((doc) => {
          const journeyData = doc.data();
          const journeyId = doc.id;
          const journeyLi = document.createElement("li"); // Use <li> for list items
          journeyLi.className = "journey-item";
          journeyLi.innerHTML = `<strong>ID:</strong> ${journeyId} <br>
                                    <small>Last update: ${
                                      journeyData.last_updated_server
                                        ? journeyData.last_updated_server
                                            .toDate()
                                            .toLocaleTimeString()
                                        : "N/A"
                                    }</small>`;
          journeyLi.style.cursor = "pointer";
          journeyLi.style.padding = "8px 0";
          journeyLi.style.borderBottom = "1px dotted #ccc";

          journeyLi.onclick = () => {
            activeJourneyId = journeyId; // Set the active journey for live updates
            console.log(`Selected Journey for display: ${activeJourneyId}`);
            displayJourneyRoute(activeJourneyId); // Display the full historical route
            setupLiveLocationListener(activeJourneyId); // Setup live listener for this selected journey
          };
          journeyListDiv.appendChild(journeyLi);
        });

        // Automatically load the latest journey's route and live data on page load
        // Only auto-load if no journey selected yet AND there are journeys available
        if (snapshot.docs.length > 0 && activeJourneyId === null) {
          const latestJourneyId = snapshot.docs[0].id;
          console.log("Auto-loading latest journey:", latestJourneyId);
          activeJourneyId = latestJourneyId;
          setupLiveLocationListener(activeJourneyId); // Listen for live updates
          displayJourneyRoute(activeJourneyId); // Display its historical route
        }
      },
      (error) => {
        console.error("Error setting up journey list listener:", error);
        journeyListDiv.innerHTML = "<p>Error loading journeys.</p>";
      }
    );
}

// Listens for live location updates for the currently active journey
// This function sets up a new listener whenever activeJourneyId changes
let liveLocationUnsubscribe = null; // Variable to store the unsubscribe function (Firestore unsubscribe)

function setupLiveLocationListener(journeyId) {
  // Unsubscribe from previous listener if it exists
  if (liveLocationUnsubscribe) {
    liveLocationUnsubscribe();
    console.log("Unsubscribed from previous live location listener.");
  }

  if (!journeyId) {
    console.warn("No journey ID provided for live location listener.");
    // Clear live data display
    lastUpdatedSpan.textContent = "N/A";
    latitudeSpan.textContent = "N/A";
    longitudeSpan.textContent = "N/A";
    speedSpan.textContent = "N/A";
    if (currentJourneyPolyline) currentJourneyPolyline.setLatLngs([]); // Clear live polyline
    return;
  }

  console.log("Setting up live location listener for Journey ID:", journeyId);
  // Listen for changes to the 'points' sub-collection for the given journey
  // Order by 'receivedAt' (server timestamp) in descending order
  // Limit to 1 to get only the latest point
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
          // Update UI and map
          updateMapAndLiveDisplay(
            latestPoint.latitude,
            latestPoint.longitude,
            latestPoint.speed_kmph || 0, // Default to 0 if speed is null
            latestPoint.total_distance_meters || 0, // Default to 0 if distance is null
            latestPoint.receivedAt // Use receivedAt as timestamp for display update
          );
        } else {
          console.log("No live data yet for selected journey.");
        }
      },
      (error) => {
        console.error("Error getting live GPS point:", error);
      }
    );
}

// --- Mode Switching Logic ---
// Use Realtime Database for config, as it's often simpler for direct device commands
const deviceConfigRef = rtdb.ref("devices/" + TEST_DEVICE_ID + "/config");
const deviceStatusRef = rtdb.ref("devices/" + TEST_DEVICE_ID + "/status"); // Device reports its status here

// Listener for when the "Set Mode" button is clicked
if (setModeButton) {
  // Check if button exists in HTML
  setModeButton.addEventListener("click", () => {
    const selectedMode = modeSelect.value;
    console.log(
      `Attempting to set mode for device ${TEST_DEVICE_ID} to: ${selectedMode}`
    );

    deviceConfigRef
      .update({
        mode: selectedMode,
        timestamp: firebase.database.ServerValue.TIMESTAMP,
      })
      .then(() => {
        console.log("Mode updated successfully in Firebase Realtime Database!");
        alert(
          `Mode set to "${selectedMode}" for device "${TEST_DEVICE_ID}". Device should update shortly.`
        );
      })
      .catch((error) => {
        console.error("Error setting mode:", error);
        alert("Failed to set mode. Check console for details.");
      });
  });
}

// Listener for when the "Start Web App Demo Mode" button is clicked
let demoIntervalId = null; // To store the ID of the setInterval timer for demo mode

if (startDemoModeButton) {
  // Check if button exists in HTML
  startDemoModeButton.addEventListener("click", () => {
    console.log("Starting Web App Demo Mode...");

    // Hide Start button, show Stop button
    startDemoModeButton.style.display = "none";
    if (stopDemoModeButton) stopDemoModeButton.style.display = "inline-block"; // Ensure stop button exists

    // Immediately send the first request
    sendDemoTriggerToCloudFunction();

    // Set up an interval to send requests repeatedly
    // This interval should roughly match your ESP32's DATA_SEND_INTERVAL_MS (e.g., 10 seconds)
    demoIntervalId = setInterval(sendDemoTriggerToCloudFunction, 10000); // Calls every 10 seconds
  });
}

// Listener for when the "Stop Demo Mode" button is clicked
if (stopDemoModeButton) {
  // Check if button exists in HTML
  stopDemoModeButton.addEventListener("click", () => {
    console.log("Stopping Web App Demo Mode...");
    if (demoIntervalId) {
      clearInterval(demoIntervalId); // Stop the interval timer
      demoIntervalId = null;
    }
    // Hide Stop button, show Start button
    stopDemoModeButton.style.display = "none";
    startDemoModeButton.style.display = "inline-block";
    alert("Web App Demo Mode stopped.");
  });
}

// New helper function to send the demo trigger to the Cloud Function
function sendDemoTriggerToCloudFunction() {
  const cloudFunctionUrl =
    "https://us-central1-gps-tracker-59cb6.cloudfunctions.net/receiveGPSData"; // Your Cloud Function URL

  fetch(cloudFunctionUrl, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ demoMode: true }), // Send the demoMode flag
  })
    .then((response) => {
      if (!response.ok) {
        // Check if response is 4xx or 5xx HTTP errors
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      return response.json(); // Parse JSON response
    })
    .then((data) => {
      console.log("Cloud Function response (Demo Mode):", data);
      // Optional: You could update a status message on the page here
    })
    .catch((error) => {
      console.error("Error sending demo trigger to Cloud Function:", error);
      // If there's an error, stop the interval to prevent spamming
      if (demoIntervalId) {
        clearInterval(demoIntervalId);
        demoIntervalId = null;
        startDemoModeButton.style.display = "inline-block";
        stopDemoModeButton.style.display = "none";
      }
      alert("Failed to send demo data. Demo stopped. Check console.");
    });
}

// Listen for the device's CURRENT reported mode (if the device sends it back)
// This makes the UI more robust by showing what the device *actually* set itself to.
deviceStatusRef.on(
  "value",
  (snapshot) => {
    const statusData = snapshot.val();
    if (statusData && statusData.currentMode) {
      modeSelect.value = statusData.currentMode; // Update dropdown
      lastSetModeDisplay.textContent = statusData.currentMode; // Update text display
      console.log(
        "Device is currently reporting mode:",
        statusData.currentMode
      );
    }
  },
  (error) => {
    console.error("Error listening to device status:", error);
  }
);

// --- Initialize the dashboard when the page loads ---
document.addEventListener("DOMContentLoaded", () => {
  // Load Leaflet map
  initializeMap();
  // Setup listeners for journey list and auto-load latest
  setupJourneyListListener();
});
