import React from 'react';

// Without this, any render-time exception unmounts the whole tree and the
// browser shows a blank white page with the cause buried in the console.
export default class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = { error: null };
  }

  static getDerivedStateFromError(error) {
    return { error };
  }

  componentDidCatch(error, info) {
    console.error('Dashboard crashed:', error, info.componentStack);
  }

  render() {
    if (!this.state.error) return this.props.children;

    return (
      <div className="min-h-screen bg-slate-50 flex items-center justify-center p-6">
        <div className="max-w-xl w-full bg-white border border-slate-200 rounded-2xl shadow-sm p-8">
          <h1 className="text-lg font-bold text-red-600 mb-2">The dashboard hit an error</h1>
          <p className="text-sm text-slate-500 mb-4">
            Details are below and in the browser console.
          </p>
          <pre className="text-xs bg-slate-50 border border-slate-200 rounded-lg p-4 overflow-x-auto whitespace-pre-wrap text-slate-700">
            {String(this.state.error?.stack || this.state.error)}
          </pre>
          <button
            onClick={() => window.location.reload()}
            className="mt-4 text-xs font-bold text-blue-600 hover:bg-blue-50 px-3 py-1.5 rounded-lg transition-colors"
          >
            Reload
          </button>
        </div>
      </div>
    );
  }
}
