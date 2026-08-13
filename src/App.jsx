import React, { useState, useEffect, useMemo } from 'react';
import { db } from './firebase';
import { ref, onValue, off } from 'firebase/database';
import { ShieldAlert, Zap, Radio, Database, MapPin, Activity, History, ChevronRight, Bell, BellOff } from 'lucide-react';
import { Tooltip, ResponsiveContainer, AreaChart, Area, XAxis, YAxis, CartesianGrid } from 'recharts';
import Header from './components/Header';
import MapComponent from './components/Map';

export default function App() {
    const [devices, setDevices] = useState({});
    const [selectedId, setSelectedId] = useState(null);
    const [isMuted, setIsMuted] = useState(false);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const devicesRef = ref(db, 'devices');
        onValue(devicesRef, (snapshot) => {
            const data = snapshot.val() || {};
            setDevices(data);
            if (!selectedId && Object.keys(data).length > 0) setSelectedId(Object.keys(data)[0]);
            setLoading(false);
        });
        return () => off(devicesRef);
    }, [selectedId]);

    const activeDevice = devices[selectedId] || null;
    const status = activeDevice?.status || {};
    const events = activeDevice?.events ? Object.entries(activeDevice.events)
        .map(([id, val]) => ({ id, ...val }))
        .sort((a, b) => b.ts - a.ts) : [];

    const getThreatUI = (lvl) => {
        const config = {
            0: { label: 'System Clear', color: 'text-emerald-400', bg: 'bg-emerald-500/10', border: 'border-emerald-500/20', shadow: 'shadow-emerald-500/20' },
            1: { label: 'Watch Active', color: 'text-amber-400', bg: 'bg-amber-500/10', border: 'border-amber-500/20', shadow: 'shadow-amber-500/20' },
            2: { label: 'Severe Warning', color: 'text-orange-500', bg: 'bg-orange-500/10', border: 'border-orange-500/20', shadow: 'shadow-orange-500/20' },
            3: { label: 'Take Shelter', color: 'text-red-500', bg: 'bg-red-500/20', border: 'border-red-500/40', shadow: 'shadow-red-500/40', pulse: 'threat-pulse' }
        };
        return config[lvl] || config[0];
    };

    const ui = getThreatUI(status.currentLevel);

    if (loading) return (
        <div className="h-screen w-full flex flex-col items-center justify-center bg-[#0f172a] p-4 text-center">
            <Zap className="w-10 h-10 text-blue-500 animate-bounce mb-4" />
            <p className="text-blue-200/50 font-mono text-xs tracking-[0.3em] animate-pulse uppercase">Link Establising...</p>
        </div>
    );

    return (
        <div className="min-h-screen pb-6 md:pb-10">
            {/* RESPONSIVE NAV */}
            <nav className="glass-card !rounded-none border-t-0 border-x-0 h-16 sticky top-0 z-50 px-4 md:px-6 flex items-center justify-between">
                <div className="flex items-center gap-2">
                    <div className="bg-blue-600 p-1 rounded-lg">
                        <Zap className="text-white w-4 h-4 fill-current" />
                    </div>
                    <span className="font-black tracking-tighter text-base md:text-xl text-white">LIGHTNING<span className="text-blue-500">CORE</span></span>
                </div>

                <div className="flex items-center gap-3 md:gap-6">
                    <select
                        value={selectedId || ''}
                        onChange={(e) => setSelectedId(e.target.value)}
                        className="bg-white/5 border border-white/10 rounded-full px-3 py-1 text-[11px] md:text-sm font-medium text-white max-w-[120px] md:max-w-none"
                    >
                        {Object.entries(devices).map(([id, dev]) => (
                            <option key={id} value={id} className="bg-[#0f172a]">{dev.meta?.name || id}</option>
                        ))}
                    </select>
                    <button onClick={() => setIsMuted(!isMuted)} className="text-slate-400">
                        {isMuted ? <BellOff size={18} /> : <Bell size={18} />}
                    </button>
                </div>
            </nav>

            <main className="max-w-[1600px] mx-auto p-3 md:p-8 space-y-4 md:space-y-8">

                {/* TOP SECTION: Stacks on mobile, Side-by-side on LG */}
                <div className="grid grid-cols-1 lg:grid-cols-12 gap-4 md:gap-8">

                    {/* Main Threat Badge */}
                    <div className="lg:col-span-5 flex flex-col gap-4">
                        <div className={`glass-card p-6 md:p-10 flex flex-col items-center justify-center text-center transition-all duration-700 ${ui.bg} ${ui.border} ${ui.shadow} ${ui.pulse}`}>
                            <p className="text-[10px] font-black uppercase tracking-[0.2em] opacity-60 mb-2">Live Alert Status</p>
                            <h2 className={`text-3xl sm:text-4xl md:text-6xl font-black uppercase tracking-tighter mb-4 ${ui.color}`}>
                                {ui.label}
                            </h2>
                            <div className="flex gap-6 md:gap-12 mt-2">
                                <div className="text-center">
                                    <p className="text-[9px] uppercase font-bold opacity-40">Distance</p>
                                    <p className="text-xl md:text-3xl font-black text-white">{events[0]?.distanceKm || '--'}<span className="text-xs opacity-40 ml-0.5 md:ml-1 font-normal">KM</span></p>
                                </div>
                                <div className="w-px h-8 md:h-12 bg-white/10" />
                                <div className="text-center">
                                    <p className="text-[9px] uppercase font-bold opacity-40">Signal</p>
                                    <p className="text-xl md:text-3xl font-black text-white">{status.rssi || '-00'}<span className="text-xs opacity-40 ml-0.5 md:ml-1 font-normal">dBm</span></p>
                                </div>
                            </div>
                        </div>

                        {/* Quick Stats Grid: 2 columns on mobile, 3 on tablet+ */}
                        <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
                            <StatCard icon={<Activity size={14} />} label="Storm Energy" value={events[0]?.energy || 'Low'} />
                            <StatCard icon={<Database size={14} />} label="Events" value={events.length} />
                            <div className="col-span-2 md:col-span-1">
                                <StatCard icon={<Radio size={14} />} label="Station" value={status.online ? 'Online' : 'Offline'} />
                            </div>
                        </div>
                    </div>

                    {/* Map Overlay: Height fixed for mobile */}
                    <div className="lg:col-span-7 glass-card overflow-hidden h-[300px] md:h-auto min-h-[300px]">
                        <MapComponent meta={activeDevice?.meta} level={status.currentLevel} />
                    </div>
                </div>

                {/* BOTTOM SECTION */}
                <div className="grid grid-cols-1 lg:grid-cols-12 gap-4 md:gap-8">

                    {/* Live Trend Chart: Reduced height on small screens */}
                    <div className="lg:col-span-8 glass-card p-4 md:p-6">
                        <div className="flex justify-between items-center mb-6">
                            <h3 className="text-[10px] md:text-xs font-black uppercase tracking-widest flex items-center gap-2">
                                <Activity size={16} className="text-blue-500" /> Storm Approach Profile
                            </h3>
                        </div>
                        <div className="h-[220px] md:h-[350px] w-full">
                            <ResponsiveContainer width="100%" height="100%">
                                <AreaChart data={events.slice(0, 20).reverse()}>
                                    <defs>
                                        <linearGradient id="colorDist" x1="0" y1="0" x2="0" y2="1">
                                            <stop offset="5%" stopColor="#3b82f6" stopOpacity={0.3} />
                                            <stop offset="95%" stopColor="#3b82f6" stopOpacity={0} />
                                        </linearGradient>
                                    </defs>
                                    <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="rgba(255,255,255,0.05)" />
                                    <XAxis dataKey="ts" hide />
                                    <YAxis reversed domain={[0, 40]} stroke="rgba(255,255,255,0.3)" fontSize={9} />
                                    {/* Inside your AreaChart component */}
                                    <Tooltip
                                        // Add this labelFormatter line:
                                        labelFormatter={(label) => new Date(label).toLocaleTimeString('en-GB', {
                                            hour: '2-digit',
                                            minute: '2-digit',
                                            second: '2-digit',
                                            hour12: true
                                        })}
                                        contentStyle={{
                                            backgroundColor: '#1e293b',
                                            borderRadius: '12px',
                                            border: '1px solid rgba(255,255,255,0.1)',
                                            color: '#fff'
                                        }}
                                        itemStyle={{ color: '#3b82f6', fontSize: '12px', fontWeight: 'bold' }}
                                    />
                                    <Area type="monotone" dataKey="distanceKm" stroke="#3b82f6" strokeWidth={3} fillOpacity={1} fill="url(#colorDist)" />
                                </AreaChart>
                            </ResponsiveContainer>
                        </div>
                    </div>

                    {/* Sequence Log: Compact for mobile */}
                    <div className="lg:col-span-4 glass-card p-4 md:p-6 flex flex-col">
                        <h3 className="text-[10px] md:text-xs font-black uppercase tracking-widest flex items-center gap-2 mb-4">
                            <History size={16} className="text-blue-500" /> Sequence Log
                        </h3>
                        <div className="space-y-2 overflow-y-auto max-h-[250px] md:max-h-[350px] pr-1">
                            {events.map((ev, i) => (
                                <div key={i} className="flex items-center justify-between p-2.5 rounded-xl bg-white/5 border border-white/5 text-[11px]">
                                    <div className="flex items-center gap-2">
                                        <div className={`w-1 h-4 rounded-full ${getThreatUI(ev.level).color.replace('text', 'bg')}`} />
                                        <span className="font-bold uppercase tracking-tight text-white">{ev.type}</span>
                                    </div>
                                    <span className="opacity-40 font-mono">{new Date(ev.ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}</span>
                                    <span className="font-black text-blue-400">{ev.distanceKm}km</span>
                                </div>
                            ))}
                        </div>
                    </div>
                </div>
            </main>
        </div>
    );
}

function StatCard({ icon, label, value }) {
    return (
        <div className="glass-card p-3 flex items-center gap-3 border-white/5">
            <div className="p-1.5 bg-blue-500/10 rounded-lg text-blue-400 shrink-0">
                {icon}
            </div>
            <div className="min-w-0">
                <p className="text-[8px] font-bold uppercase opacity-40 tracking-tight truncate">{label}</p>
                <p className="text-[11px] md:text-sm font-black text-white truncate">{value}</p>
            </div>
        </div>
    );
}