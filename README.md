# Lightning-EWS

Dashboard for the IoT lightning early-warning nodes (ESP32 + AS3935 + SIM800L).

## Running

```bash
npm install
cp .env.example .env   # fill in your Firebase project values
npm run dev
```

## Expected Realtime Database shape

The dashboard reads from `/devices`. Each node writes:

```
devices/
  <deviceId>/
    meta/    { name: "Field Node 1", lat: 23.8103, lng: 90.4125 }
    status/  { currentLevel: 0..3, rssi: -67, lastSeen: <epoch ms> }
    events/
      <pushId>/ { ts: <epoch ms>, type: "strike", level: 0..3, distanceKm: 12 }
```

`lastSeen` drives the online/offline badge — a node is shown offline after five
minutes without an update. Levels match the firmware thresholds: 0 clear,
1 watch (>25 km), 2 warning (10–25 km), 3 shelter (≤10 km).
