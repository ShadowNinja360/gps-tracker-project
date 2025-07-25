const functions = require("firebase-functions");
const admin = require("firebase-admin");
admin.initializeApp();

exports.receiveGPSData = functions.https.onRequest(async (req, res) => {
  // 1. Handle CORS Preflight request (OPTIONS method)
  // The browser sends an OPTIONS request before the actual POST request
  // to check if the server accepts the cross-origin request.
  if (req.method === "OPTIONS") {
    res.set("Access-Control-Allow-Origin", "*"); // Allow requests from any origin (for testing)
    res.set("Access-Control-Allow-Methods", "POST, OPTIONS"); // Allow POST and OPTIONS methods
    res.set("Access-Control-Allow-Headers", "Content-Type"); // Allow Content-Type header
    res.set("Access-Control-Max-Age", "3600"); // Cache preflight response for 1 hour
    return res.status(204).send(""); // 204 No Content for successful preflight
  }

  // 2. Set CORS headers for the actual POST request
  // This must be done for all responses that are accessed cross-origin
  res.set("Access-Control-Allow-Origin", "*"); // Allow requests from any origin (for testing)
  // For production, replace '*' with your specific domain: e.g., 'https://gps-tracker-59cb6.web.app'

  // 3. Ensure it's a POST request for data processing
  if (req.method !== "POST") {
    return res
      .status(405)
      .send("Method Not Allowed. Only POST requests are accepted.");
  }

  const db = admin.firestore();
  let lat, lon, speed, distance, timestamp, journeyId;

  // --- Demo Mode Logic ---
  if (req.body.demoMode === true) {
    const currentHour = new Date().getHours();
    const baseLat = 28.6139; // Delhi Latitude
    const baseLon = 77.209; // Delhi Longitude

    const offsetLat = Math.sin(Date.now() / 600000) * 0.05;
    const offsetLon = Math.cos(Date.now() / 600000) * 0.08;

    lat = baseLat + offsetLat;
    lon = baseLon + offsetLon;
    speed = Math.random() * 20 + 5;
    distance = Math.random() * 5000 + 1000;
    timestamp = Date.now();
    journeyId = "DEMO_JOURNEY_" + currentHour;

    console.log(
      `[DEMO MODE] Generating dummy data: JourneyID=${journeyId}, Lat=${lat.toFixed(
        6
      )}, Lon=${lon.toFixed(6)}`
    );
  } else {
    // --- Real Device Data Logic ---
    const {
      journey_id,
      latitude,
      longitude,
      speed_kmph,
      total_distance_meters,
      timestamp: deviceTimestamp,
    } = req.body;

    if (
      !journey_id ||
      latitude === undefined ||
      longitude === undefined ||
      !deviceTimestamp
    ) {
      return res.status(400).send("Missing required parameters for real data.");
    }

    journeyId = String(journey_id);
    lat = parseFloat(latitude);
    lon = parseFloat(longitude);
    speed = parseFloat(speed_kmph);
    distance = parseFloat(total_distance_meters);
    timestamp = deviceTimestamp; // Use device's timestamp
  }

  // --- Store Data to Firestore ---
  try {
    const journeyRef = db.collection("journeys").doc(journeyId);

    await journeyRef.collection("points").add({
      latitude: lat,
      longitude: lon,
      speed_kmph: isNaN(speed) ? null : speed,
      total_distance_meters: isNaN(distance) ? null : distance,
      timestamp: admin.firestore.Timestamp.fromMillis(parseInt(timestamp)),
      receivedAt: admin.firestore.FieldValue.serverTimestamp(),
    });

    await journeyRef.set(
      {
        last_latitude: lat,
        last_longitude: lon,
        last_speed_kmph: isNaN(speed) ? null : speed,
        last_total_distance_meters: isNaN(distance) ? null : distance,
        last_updated_device: admin.firestore.Timestamp.fromMillis(
          parseInt(timestamp)
        ),
        last_updated_server: admin.firestore.FieldValue.serverTimestamp(),
        is_active: true,
      },
      { merge: true }
    );

    return res
      .status(200)
      .json({ status: "success", message: "Data received and stored." });
  } catch (error) {
    console.error("Error writing to Firestore:", error);
    return res.status(500).send("Error processing data: " + error.message);
  }
});
