import { MapContainer, TileLayer, Marker, Popup, Circle } from 'react-leaflet';
import 'leaflet/dist/leaflet.css';
import L from 'leaflet';

// Fix for default Leaflet marker icons in React
import markerIcon from 'leaflet/dist/images/marker-icon.png';
import markerShadow from 'leaflet/dist/images/marker-shadow.png';
let DefaultIcon = L.icon({ iconUrl: markerIcon, shadowUrl: markerShadow, iconSize: [25, 41], iconAnchor: [12, 41] });
L.Marker.prototype.options.icon = DefaultIcon;

const COLORS = ['#64748b', '#f59e0b', '#f97316', '#dc2626'];

export default function MapComponent({ meta, level = 0 }) {
  const lat = Number(meta?.lat);
  const lng = Number(meta?.lng);
  // Default to Dhaka when the node has not reported coordinates.
  const position = [Number.isFinite(lat) ? lat : 23.8103, Number.isFinite(lng) ? lng : 90.4125];
  const active = COLORS[level] || COLORS[0];

  return (
    <div className="bg-white p-2 rounded-2xl shadow-sm border border-slate-200 h-[350px] overflow-hidden">
      {/* react-leaflet ignores `center` changes after mount, so remount on move. */}
      <MapContainer
        key={`${position[0]},${position[1]}`}
        center={position}
        zoom={10}
        style={{ height: '100%', width: '100%', borderRadius: '12px' }}
      >
        <TileLayer
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        />
        <Marker position={position}>
          <Popup>{meta?.name || 'Device Location'}</Popup>
        </Marker>
        {/* Shelter (<=10 km) and warning (<=25 km) rings, per the firmware thresholds. */}
        <Circle center={position} radius={10000} pathOptions={{ color: COLORS[3], fillOpacity: 0.1 }} />
        <Circle center={position} radius={25000} pathOptions={{ color: COLORS[2], fillOpacity: 0.05 }} />
        <Circle center={position} radius={600} pathOptions={{ color: active, fillColor: active, fillOpacity: 0.9, weight: 0 }} />
      </MapContainer>
    </div>
  );
}