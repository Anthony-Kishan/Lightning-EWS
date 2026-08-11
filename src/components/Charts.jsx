import { useMemo } from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';

export default function Charts({ events = [] }) {
  const chartData = useMemo(
    () =>
      events
        .slice(0, 20)
        .reverse() // slice() copies first, so this does not mutate the prop
        .map((e) => ({
          time: new Date(e.ts).toLocaleTimeString(),
          dist: e.distanceKm,
        })),
    [events]
  );

  return (
    <div className="bg-white p-6 rounded-2xl shadow-sm border border-slate-200 h-[350px]">
      <h3 className="text-sm font-bold text-slate-400 uppercase mb-4">Storm Approach (km)</h3>
      {chartData.length === 0 ? (
        <div className="h-[90%] flex items-center justify-center text-sm text-slate-400">
          No strike data yet.
        </div>
      ) : (
      <ResponsiveContainer width="100%" height="90%">
        <LineChart data={chartData}>
          <CartesianGrid strokeDasharray="3 3" vertical={false} />
          <XAxis dataKey="time" hide />
          <YAxis reversed domain={[0, 40]} />
          <Tooltip />
          <Line type="monotone" dataKey="dist" stroke="#2563eb" strokeWidth={3} dot={{ fill: '#2563eb' }} />
        </LineChart>
      </ResponsiveContainer>
      )}
    </div>
  );
}