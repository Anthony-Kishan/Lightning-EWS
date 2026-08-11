import { Zap, Radio, ChevronDown } from 'lucide-react';

export default function Header({ devices, selectedId, setSelectedId, isOnline }) {
  const activeDevice = devices[selectedId];

  return (
    <header className="bg-white border-b border-slate-200 sticky top-0 z-50">
      <div className="max-w-7xl mx-auto px-4 h-16 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <div className="bg-blue-600 p-1.5 rounded-lg">
            <Zap className="text-white w-5 h-5 fill-current" />
          </div>
          <h1 className="font-bold text-lg hidden sm:block tracking-tight">LEWS DASHBOARD</h1>
        </div>

        <div className="flex items-center gap-4">
          <div className="relative group">
            <select 
              value={selectedId || ''} 
              onChange={(e) => setSelectedId(e.target.value)}
              className="appearance-none bg-slate-100 border-none rounded-full px-4 py-1.5 pr-8 text-sm font-semibold cursor-pointer focus:ring-2 focus:ring-blue-500"
            >
              {Object.entries(devices).map(([id, dev]) => (
                <option key={id} value={id}>{dev.meta?.name || id}</option>
              ))}
            </select>
            <ChevronDown size={14} className="absolute right-3 top-2.5 pointer-events-none opacity-50" />
          </div>

          <div className={`flex items-center gap-1.5 px-3 py-1 rounded-full text-[10px] font-black uppercase tracking-widest ${isOnline ? 'bg-green-100 text-green-700' : 'bg-red-100 text-red-700'}`}>
            <span className={`w-2 h-2 rounded-full ${isOnline ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`} />
            {isOnline ? 'System Live' : 'System Offline'}
          </div>
        </div>
      </div>
    </header>
  );
}