import { useEffect, useMemo, useState } from 'react';
import { ref, onValue, query, limitToLast } from 'firebase/database';
import { db, configError, authReady } from './firebase';
import Header from './components/Header';
import StatusPanel from './components/StatusPanel';
import MapComponent from './components/Map';
import Charts from './components/Charts';
import EventFeed from './components/EventFeed';
import HistoryTable from './components/HistoryTable';

// A node is considered offline if it has not checked in for this long.
const OFFLINE_AFTER_MS = 5 * 60 * 1000;
const MAX_EVENTS = 200;

// Firebase pushes objects keyed by push-id; the components want a sorted array.
function toEventArray(raw) {
  if (!raw) return [];
  return Object.entries(raw)
    .map(([id, e]) => ({
      id,
      ts: Number(e?.ts) || 0,
      type: typeof e?.type === 'string' ? e.type : 'unknown',
      level: Number(e?.level) || 0,
      distanceKm: Number(e?.distanceKm) || 0,
    }))
    .sort((a, b) => b.ts - a.ts); // newest first
}

export default function App() {
  const [devices, setDevices] = useState({});
  const [selectedId, setSelectedId] = useState(null);
  const [events, setEvents] = useState([]);
  const [error, setError] = useState(configError);
  const [loading, setLoading] = useState(!configError);
  const [now, setNow] = useState(() => Date.now());

  // Subscribe to the device registry (meta + status for every node).
  useEffect(() => {
    if (!db) return;
    let cancelled = false;

    authReady.catch((err) => {
      if (!cancelled) {
        setError(err.message);
        setLoading(false);
      }
    });

    const unsubscribe = onValue(
      ref(db, 'devices'),
      (snap) => {
        if (cancelled) return;
        setDevices(snap.val() || {});
        setLoading(false);
      },
      (err) => {
        if (cancelled) return;
        setError(`Could not read /devices: ${err.message}`);
        setLoading(false);
      }
    );

    return () => {
      cancelled = true;
      unsubscribe();
    };
  }, []);

  const deviceIds = useMemo(() => Object.keys(devices), [devices]);

  // Keep the selection valid as devices appear or disappear.
  useEffect(() => {
    if (deviceIds.length === 0) {
      if (selectedId !== null) setSelectedId(null);
    } else if (!selectedId || !deviceIds.includes(selectedId)) {
      setSelectedId(deviceIds[0]);
    }
  }, [deviceIds, selectedId]);

  // Subscribe to the selected device's event log.
  useEffect(() => {
    if (!db || !selectedId) {
      setEvents([]);
      return;
    }
    let cancelled = false;

    const unsubscribe = onValue(
      query(ref(db, `devices/${selectedId}/events`), limitToLast(MAX_EVENTS)),
      (snap) => {
        if (!cancelled) setEvents(toEventArray(snap.val()));
      },
      (err) => {
        if (!cancelled) setError(`Could not read events: ${err.message}`);
      }
    );

    return () => {
      cancelled = true;
      unsubscribe();
    };
  }, [selectedId]);

  // Drives the relative timestamps in the feed and the online/offline badge.
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), 30 * 1000);
    return () => clearInterval(id);
  }, []);

  const active = (selectedId && devices[selectedId]) || null;
  const status = active?.status || {};
  const meta = active?.meta || {};
  const lastSeen = Number(status.lastSeen) || 0;
  const isOnline = lastSeen > 0 && now - lastSeen < OFFLINE_AFTER_MS;
  const lastStrike = events.find((e) => e.distanceKm > 0) || null;

  if (error) {
    return <Fallback title="Configuration problem" detail={error} />;
  }
  if (loading) {
    return <Fallback title="Connecting to Firebase…" detail="Loading device registry." />;
  }
  if (deviceIds.length === 0) {
    return (
      <Fallback
        title="No devices reporting"
        detail="Nothing has been written to /devices yet. Once a node publishes its meta and status, it will appear here."
      />
    );
  }

  return (
    <div className="min-h-screen bg-slate-50">
      <Header
        devices={devices}
        selectedId={selectedId}
        setSelectedId={setSelectedId}
        isOnline={isOnline}
      />

      <main className="max-w-7xl mx-auto px-4 py-6 space-y-6">
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          <StatusPanel status={status} lastStrike={lastStrike} />
          <div className="lg:col-span-2">
            <MapComponent meta={meta} level={status.currentLevel || 0} />
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          <div className="lg:col-span-2">
            <Charts events={events} />
          </div>
          <EventFeed events={events} now={now} />
        </div>

        <HistoryTable events={events} deviceName={meta.name || selectedId} />
      </main>
    </div>
  );
}

function Fallback({ title, detail }) {
  return (
    <div className="min-h-screen bg-slate-50 flex items-center justify-center p-6">
      <div className="max-w-lg bg-white border border-slate-200 rounded-2xl shadow-sm p-8 text-center">
        <h1 className="text-lg font-bold mb-2">{title}</h1>
        <p className="text-sm text-slate-500 leading-relaxed">{detail}</p>
      </div>
    </div>
  );
}
