import React, { useState, useEffect, useMemo } from 'react';
import { db } from './firebase';
import { ref, onValue, off, query, limitToLast } from 'firebase/database';
import Header from './components/Header';
import StatusPanel from './components/StatusPanel';
import EventFeed from './components/EventFeed';
import Charts from './components/Charts';
import MapComponent from './components/Map';
import HistoryTable from './components/HistoryTable';
import { ShieldAlert, Volume2, VolumeX } from 'lucide-react';

export default function App() {
  const [devices, setDevices] = useState({});
  const [selectedId, setSelectedId] = useState(null);
  const [isMuted, setIsMuted] = useState(false);
  const [loading, setLoading] = useState(true);

  // Sound effect for Level 3
  const alertSound = useMemo(() => new Audio('https://actions.google.com/sounds/v1/alarms/beep_short.ogg'), []);

  useEffect(() => {
    const devicesRef = ref(db, 'devices');
    onValue(devicesRef, (snapshot) => {
      const data = snapshot.val() || {};
      setDevices(data);
      if (!selectedId && Object.keys(data).length > 0) {
        setSelectedId(Object.keys(data)[0]);
      }
      setLoading(false);
    });
    return () => off(devicesRef);
  }, [selectedId]);

  const activeDevice = devices[selectedId] || null;
  const status = activeDevice?.status || {};
  const events = activeDevice?.events ? Object.entries(activeDevice.events)
    .map(([id, val]) => ({ id, ...val }))
    .sort((a, b) => b.ts - a.ts) : [];

  // Trigger Sound on Level 3
  useEffect(() => {
    if (status.currentLevel === 3 && !isMuted) {
      alertSound.play().catch(e => console.log("Audio play blocked by browser"));
    }
  }, [status.currentLevel, isMuted, alertSound]);

  if (loading) return (
    <div className="h-screen w-full flex items-center justify-center bg-slate-900 text-white">
      <div className="animate-spin rounded-full h-12 w-12 border-t-2 border-blue-500"></div>
    </div>
  );

  return (
    <div className="min-h-screen bg-slate-50 pb-12">
      <Header 
        devices={devices} 
        selectedId={selectedId} 
        setSelectedId={setSelectedId} 
        isOnline={status.lastSeen > Date.now() - 90000}
      />

      <main className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 mt-6">
        {/* Level 3 Emergency Banner */}
        {status.currentLevel === 3 && (
          <div className="mb-6 bg-red-600 rounded-xl p-4 text-white flex items-center justify-between shadow-lg animate-pulse">
            <div className="flex items-center gap-4">
              <ShieldAlert size={32} className="shrink-0" />
              <div>
                <h2 className="font-bold text-xl uppercase tracking-tight">Immediate Threat: Take Shelter</h2>
                <p className="text-sm opacity-90">Lightning strike detected within 10km.</p>
              </div>
            </div>
            <button 
              onClick={() => setIsMuted(!isMuted)}
              className="p-2 bg-white/20 hover:bg-white/30 rounded-full transition-colors"
            >
              {isMuted ? <VolumeX size={24} /> : <Volume2 size={24} />}
            </button>
          </div>
        )}

        {/* Dashboard Grid */}
        <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
          {/* Left Column: Live Status & Feed */}
          <div className="lg:col-span-4 space-y-6">
            <StatusPanel status={status} lastStrike={events[0]} />
            <EventFeed events={events.slice(0, 50)} />
          </div>

          {/* Right Column: Analytics & Map */}
          <div className="lg:col-span-8 space-y-6">
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <Charts events={events} />
              <MapComponent meta={activeDevice?.meta} level={status.currentLevel} />
            </div>
            <HistoryTable events={events} deviceName={activeDevice?.meta?.name} />
          </div>
        </div>
      </main>
    </div>
  );
}