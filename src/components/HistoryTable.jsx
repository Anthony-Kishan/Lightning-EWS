// src/components/HistoryTable.jsx
import { formatInTimeZone } from 'date-fns-tz';
import { Download } from 'lucide-react';

const TZ = 'Asia/Dhaka';

// Quote every field so commas/quotes in a value cannot shift the columns.
const csvCell = (v) => `"${String(v ?? '').replace(/"/g, '""')}"`;

export default function HistoryTable({ events = [], deviceName }) {
  const downloadCSV = () => {
    const headers = 'Timestamp (UTC),Type,Level,Distance(km)\n';
    const rows = events
      .map((e) =>
        [e.ts ? new Date(e.ts).toISOString() : '', e.type, e.level, e.distanceKm]
          .map(csvCell)
          .join(',')
      )
      .join('\n');
    const blob = new Blob([headers + rows], { type: 'text/csv;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `lightning_report_${(deviceName || 'device').replace(/[^\w.-]+/g, '_')}.csv`;
    document.body.appendChild(a);
    a.click();
    a.remove();
    // Without this the blob is retained for the lifetime of the page.
    URL.revokeObjectURL(url);
  };

  return (
    <div className="bg-white rounded-2xl shadow-sm border border-slate-200 overflow-hidden">
      <div className="p-4 border-b border-slate-100 flex justify-between items-center">
        <h3 className="text-xs font-bold text-slate-400 uppercase tracking-widest">Event History</h3>
        <button
          onClick={downloadCSV}
          disabled={events.length === 0}
          className="flex items-center gap-2 text-xs font-bold text-blue-600 hover:bg-blue-50 px-3 py-1.5 rounded-lg transition-colors disabled:opacity-40 disabled:hover:bg-transparent disabled:cursor-not-allowed"
        >
          <Download size={14} /> Export CSV
        </button>
      </div>
      <div className="overflow-x-auto">
        <table className="w-full text-left border-collapse">
          <thead>
            <tr className="bg-slate-50 text-[10px] uppercase text-slate-400 font-black">
              <th className="px-6 py-3">Time (Dhaka)</th>
              <th className="px-6 py-3">Event</th>
              <th className="px-6 py-3">Distance</th>
              <th className="px-6 py-3">Level</th>
            </tr>
          </thead>
          <tbody className="text-sm divide-y divide-slate-100">
            {events.length === 0 && (
              <tr>
                <td colSpan={4} className="px-6 py-10 text-center text-slate-400">
                  No events recorded.
                </td>
              </tr>
            )}
            {events.map((e) => (
              <tr key={e.id} className="hover:bg-slate-50 transition-colors">
                <td className="px-6 py-4 font-medium text-slate-600">
                  {e.ts ? formatInTimeZone(e.ts, TZ, 'MMM dd, HH:mm:ss') : '—'}
                </td>
                <td className="px-6 py-4 capitalize">{e.type}</td>
                <td className="px-6 py-4">{e.distanceKm} km</td>
                <td className="px-6 py-4">
                  <span className={`px-2 py-0.5 rounded text-[10px] font-bold uppercase ${
                    e.level === 3 ? 'bg-red-100 text-red-600' : 
                    e.level === 2 ? 'bg-orange-100 text-orange-600' : 'bg-slate-100 text-slate-600'
                  }`}>L{e.level}</span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}