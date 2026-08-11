const CONFIGS = {
  0: { label: 'Clear', color: 'bg-gray-500', text: 'text-gray-500' },
  1: { label: 'Watch', color: 'bg-amber-500', text: 'text-amber-500' },
  2: { label: 'Warning', color: 'bg-orange-500', text: 'text-orange-500' },
  3: { label: 'Shelter', color: 'bg-red-600', text: 'text-red-600' },
};

export default function StatusPanel({ status = {}, lastStrike }) {
  // Fall back to level 0 for an unknown/out-of-range level rather than reading
  // `.color` off undefined.
  const cfg = CONFIGS[status.currentLevel] || CONFIGS[0];

  return (
    <div className="bg-white rounded-2xl shadow-sm border border-slate-200 p-6">
      <p className="text-xs font-bold text-slate-400 uppercase tracking-widest mb-4">System Status</p>
      <div className={`${cfg.color} w-full py-6 rounded-xl text-center text-white mb-6 shadow-inner`}>
        <span className="text-4xl font-black uppercase tracking-tighter">{cfg.label}</span>
      </div>
      
      <div className="grid grid-cols-2 gap-4">
        <div className="bg-slate-50 p-3 rounded-lg">
          <p className="text-[10px] text-slate-500 uppercase font-bold">Last Strike</p>
          <p className="text-lg font-bold">{lastStrike ? `${lastStrike.distanceKm} km` : 'N/A'}</p>
        </div>
        <div className="bg-slate-50 p-3 rounded-lg">
          <p className="text-[10px] text-slate-500 uppercase font-bold">Signal</p>
          <p className="text-lg font-bold text-green-600">{status.rssi || '0'} dBm</p>
        </div>
      </div>
    </div>
  );
}