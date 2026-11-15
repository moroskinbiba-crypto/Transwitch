/**
 * Non-blocking user notifications.
 *
 * `alert()` is unavailable inside the Horizon OS WebKit applet: the modal blocks
 * the single-threaded applet loop and in some firmware revisions never resolves.
 * All user-facing feedback therefore goes through this tiny pub/sub bus, which
 * the <Toaster /> component renders inside the app shell.
 */

export type ToastKind = 'info' | 'warn';

export interface ToastItem {
  id: number;
  message: string;
  kind: ToastKind;
}

type ToastListener = (item: ToastItem) => void;

const listeners = new Set<ToastListener>();
let nextId = 1;

/** Shows a toast. Safe to call before <Toaster /> mounts (falls back to console). */
export function notify(message: string, kind: ToastKind = 'info'): void {
  const item: ToastItem = { id: nextId++, message, kind };

  for (const listener of listeners) {
    listener(item);
  }

  if (listeners.size === 0) {
    console.info(`[DeskNX] ${message}`);
  }
}

/** Subscribes to toasts; returns an unsubscribe function. */
export function subscribeToasts(listener: ToastListener): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
