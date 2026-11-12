import { useEffect, useState } from 'react';
import { subscribeToasts, type ToastItem } from '../ui/toast';

const TOAST_TTL_MS = 3200;
const MAX_VISIBLE_TOASTS = 3;

/**
 * Renders transient notifications. Placed once in the app shell; consumes the
 * queue published through `notify()` instead of blocking `window.alert()`.
 */
export function Toaster() {
  const [items, setItems] = useState<ToastItem[]>([]);

  useEffect(() => {
    return subscribeToasts((item) => {
      setItems((previous) => [...previous, item].slice(-MAX_VISIBLE_TOASTS));

      window.setTimeout(() => {
        setItems((previous) => previous.filter((toast) => toast.id !== item.id));
      }, TOAST_TTL_MS);
    });
  }, []);

  if (items.length === 0) {
    return null;
  }

  return (
    <div
      aria-live="polite"
      className="pointer-events-none fixed bottom-24 left-1/2 z-[300] flex -translate-x-1/2 flex-col items-center gap-2"
    >
      {items.map((toast) => (
        <div
          key={toast.id}
          className={`max-w-[70vw] rounded-2xl border px-4 py-2 text-center text-sm font-semibold shadow-xl backdrop-blur-md ${
            toast.kind === 'warn'
              ? 'border-amber-300/60 bg-amber-500/90 text-black'
              : 'border-white/15 bg-black/80 text-white'
          }`}
        >
          {toast.message}
        </div>
      ))}
    </div>
  );
}

{"content":"import { useEffect, useState } from 'react';\nimport { subscribeToasts, type ToastItem } from '../ui/toast';\n\nconst TOAST_TTL_MS = 3200;\nconst MAX_VISIBLE_TOASTS = 3;\n\n/**\n * Renders transient notifications. Placed once in the app shell; consumes the\n * queue published through `notify()` instead of blocking `window.alert()`.\n */\nexport function Toaster() {\n  const [items, setItems] = useState<ToastItem[]>([]);\n\n  useEffect(() => {\n    return subscribeToasts((item) => {\n      setItems((previous) => [...previous, item].slice(-MAX_VISIBLE_TOASTS));\n\n      window.setTimeout(() => {\n        setItems((previous) => previous.filter((toast) => toast.id !== item.id));\n      }, TOAST_TTL_MS);\n    });\n  }, []);\n\n  if (items.length === 0) {\n    return null;\n  }\n\n  return (\n    <div\n      aria-live=\"polite\"\n      className=\"pointer-events-none fixed bottom-24 left-1/2 z-[300] flex -translate-x-1/2 flex-col items-center gap-2\"\n    >\n      {items.map((toast) => (\n        <div\n          key={toast.id}\n          className={`max-w-[70vw] rounded-2xl border px-4 py-2 text-center text-sm font-semibold shadow-xl backdrop-blur-md ${\n            toast.kind === 'warn'\n              ? 'border-amber-300/60 bg-amber-500/90 text-black'\n              : 'border-white/15 bg-black/80 text-white'\n          }`}\n        >\n          {toast.message}\n        </div>\n      ))}\n    </div>\n  );\n}\n"}