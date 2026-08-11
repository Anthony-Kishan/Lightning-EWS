import { MapContainer, TileLayer, Marker, Popup, Circle } from 'react-leaflet';
import 'leaflet/dist/leaflet.css';
import L from 'leaflet';

// Fix for default Leaflet marker icons in React
import markerIcon from 'leaflet/dist/images/marker-icon.png';
import markerShadow from 'leaflet/dist/images/marker-shadow.png';
let DefaultIcon = L.icon({ iconUrl: markerIcon, shadowUrl: markerShadow, iconSize: [25, 41], iconAnchor: [12, 41] });
L.Marker.prototype.options.icon = DefaultIcon;

export default function MapComponent({ meta, level }) {
  const position = [meta?.lat || 23.8103, meta?.lng || 90.4125]; // Default to Dhaka
  const colors = ['#64748b', '#f59e0b', '#f97316', '#dc2626'];

  return (
    <div className="bg-white p-2 rounded-2xl shadow-sm border border-slate-200 h-[350px] overflow-hidden">
      <MapContainer center={position} zoom={10} style={{ height: '100%', width: '100%', borderRadius: '12px' }}>
        <TileLayer url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png" />
        <Marker position={position}>
          <Popup>{meta?.name || 'Device Location'}</Popup>
        </Marker>
        {/* Visual range circles */}
        <Circle center={position} radius={10000} pathOptions={{ color: colors[3], fillOpacity: 0.1 }} />
        <Circle center={position} radius={25000} pathOptions={{ color: colors[2], fillOpacity: 0.05 }} />
      </MapContainer>
    </div>
  );
}