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

{"content":"/**\n * Non-blocking user notifications.\n *\n * `alert()` is unavailable inside the Horizon OS WebKit applet: the modal blocks\n * the single-threaded applet loop and in some firmware revisions never resolves.\n * All user-facing feedback therefore goes through this tiny pub/sub bus, which\n * the <Toaster /> component renders inside the app shell.\n */\n\nexport type ToastKind = 'info' | 'warn';\n\nexport interface ToastItem {\n  id: number;\n  message: string;\n  kind: ToastKind;\n}\n\ntype ToastListener = (item: ToastItem) => void;\n\nconst listeners = new Set<ToastListener>();\nlet nextId = 1;\n\n/** Shows a toast. Safe to call before <Toaster /> mounts (falls back to console). */\nexport function notify(message: string, kind: ToastKind = 'info'): void {\n  const item: ToastItem = { id: nextId++, message, kind };\n\n  for (const listener of listeners) {\n    listener(item);\n  }\n\n  if (listeners.size === 0) {\n    console.info(`[DeskNX] ${message}`);\n  }\n}\n\n/** Subscribes to toasts; returns an unsubscribe function. */\nexport function subscribeToasts(listener: ToastListener): () => void {\n  listeners.add(listener);\n  return () => {\n    listeners.delete(listener);\n  };\n}\n"}