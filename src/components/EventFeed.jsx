import { formatDistanceToNow } from 'date-fns';

export default function EventFeed({ events = [] }) {
  const getLevelColor = (lvl) => {
    const colors = ['bg-slate-400', 'bg-amber-500', 'bg-orange-500', 'bg-red-600'];
    return colors[lvl] || colors[0];
  };

  return (
    <div className="bg-white rounded-2xl shadow-sm border border-slate-200 flex flex-col h-[450px]">
      <div className="p-4 border-b border-slate-100">
        <h3 className="text-xs font-bold text-slate-400 uppercase tracking-widest">Live Strike Feed</h3>
      </div>
      <div className="flex-1 overflow-y-auto p-4 space-y-4">
        {events.length === 0 && <p className="text-center text-slate-400 text-sm py-10">No recent activity detected.</p>}
        {events.map((event) => (
          <div key={event.id} className="flex items-start gap-3 group">
            <div className={`mt-1.5 w-2 h-2 rounded-full shrink-0 ${getLevelColor(event.level)}`} />
            <div className="flex-1">
              <div className="flex justify-between items-start">
                <p className="text-sm font-bold capitalize">
                  {String(event.type || 'unknown').replace(/_/g, ' ')}
                </p>
                <span className="text-[10px] text-slate-400 font-medium">
                  {event.ts ? formatDistanceToNow(event.ts, { addSuffix: true }) : '—'}
                </span>
              </div>
              <p className="text-xs text-slate-500">
                Distance: <span className="font-semibold text-slate-700">{event.distanceKm} km</span>
              </p>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}