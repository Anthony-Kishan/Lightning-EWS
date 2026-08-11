import { initializeApp } from "firebase/app";
import { getDatabase } from "firebase/database";
import { getAuth, signInAnonymously } from "firebase/auth";

const firebaseConfig = {
  apiKey: import.meta.env.VITE_FIREBASE_API_KEY,
  authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,
  databaseURL: import.meta.env.VITE_FIREBASE_DATABASE_URL,
  projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,
  storageBucket: import.meta.env.VITE_FIREBASE_STORAGE_BUCKET,
  messagingSenderId: import.meta.env.VITE_FIREBASE_MESSAGING_SENDER_ID,
  appId: import.meta.env.VITE_FIREBASE_APP_ID,
};

// Fail loudly and early: without these the SDK throws deep inside its own code,
// which is very hard to trace back to a missing .env entry.
const REQUIRED = {
  apiKey: "VITE_FIREBASE_API_KEY",
  authDomain: "VITE_FIREBASE_AUTH_DOMAIN",
  databaseURL: "VITE_FIREBASE_DATABASE_URL",
  projectId: "VITE_FIREBASE_PROJECT_ID",
  appId: "VITE_FIREBASE_APP_ID",
};
const missing = Object.keys(REQUIRED).filter((k) => !firebaseConfig[k]);
export const configError = missing.length
  ? `Missing environment variable${missing.length > 1 ? "s" : ""}: ${missing
      .map((k) => REQUIRED[k])
      .join(", ")}. Copy .env.example to .env and fill it in, then restart the dev server.`
  : null;

const app = configError ? null : initializeApp(firebaseConfig);
const db = app ? getDatabase(app) : null;
const auth = app ? getAuth(app) : null;

// Anonymous sign-in is required by the database security rules. Keep the promise
// around so the UI can wait on it and report a readable error instead of leaving
// an unhandled rejection in the console.
const authReady = auth
  ? signInAnonymously(auth).then(
      (cred) => cred.user,
      (err) => {
        throw new Error(
          `Firebase anonymous sign-in failed (${err.code || err.message}). ` +
            `Enable Authentication > Sign-in method > Anonymous in the Firebase console.`
        );
      }
    )
  : Promise.reject(new Error(configError));

// Avoid an unhandled rejection warning when nothing has attached a handler yet.
authReady.catch(() => {});

export { db, auth, authReady };
