import { useEffect, useRef } from 'react';

export interface SwitchGamepadCallbacks {
  onNavigate?: (direction: 'up' | 'down' | 'left' | 'right') => void;
  onButtonA?: () => void;
  onButtonB?: () => void;
  onButtonX?: () => void;
  onButtonY?: () => void;
  onShoulderL?: () => void;
  onShoulderR?: () => void;
  onTriggerZL?: () => void;
  onTriggerZR?: () => void;
  onButtonPlus?: () => void;
  onButtonMinus?: () => void;
}

/**
 * Custom React hook for physical Nintendo Switch Joy-Con & Pro Controller input
 * Uses standard HTML5 Gamepad API with debouncing and axis deadzones.
 */
export function useSwitchGamepad(callbacks: SwitchGamepadCallbacks) {
  const callbacksRef = useRef(callbacks);
  callbacksRef.current = callbacks;

  const lastButtonStateRef = useRef<Record<number, boolean>>({});
  const lastAxisNavTimeRef = useRef<number>(0);
  const animFrameRef = useRef<number | null>(null);

  useEffect(() => {
    if (typeof window === 'undefined' || !navigator.getGamepads) return;

    const AXIS_THRESHOLD = 0.55;
    const AXIS_REPEAT_DELAY_MS = 220;

    const pollGamepad = () => {
      const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
      let activePad: Gamepad | null = null;

      for (let i = 0; i < gamepads.length; i++) {
        const gp = gamepads[i];
        if (gp && gp.connected) {
          activePad = gp;
          break;
        }
      }

      if (activePad) {
        const buttons = activePad.buttons;
        const now = performance.now();
        const prev = lastButtonStateRef.current;

        // Standard Gamepad Button Mapping (Nintendo Switch Joy-Con / Pro Controller)
        // 0: B, 1: A, 2: Y, 3: X
        // 4: L, 5: R, 6: ZL, 7: ZR
        // 8: -, 9: +, 10: LS, 11: RS
        // 12: D-Pad Up, 13: D-Pad Down, 14: D-Pad Left, 15: D-Pad Right
        const checkJustPressed = (btnIndex: number): boolean => {
          const isPressed = !!buttons[btnIndex]?.pressed;
          const wasPressed = !!prev[btnIndex];
          prev[btnIndex] = isPressed;
          return isPressed && !wasPressed;
        };

        // Navigation (D-Pad)
        if (checkJustPressed(12)) callbacksRef.current.onNavigate?.('up');
        if (checkJustPressed(13)) callbacksRef.current.onNavigate?.('down');
        if (checkJustPressed(14)) callbacksRef.current.onNavigate?.('left');
        if (checkJustPressed(15)) callbacksRef.current.onNavigate?.('right');

        // Analog Stick Navigation (Left Stick axes 0, 1)
        if (now - lastAxisNavTimeRef.current > AXIS_REPEAT_DELAY_MS) {
          const axisX = activePad.axes[0] || 0;
          const axisY = activePad.axes[1] || 0;

          if (Math.abs(axisX) > AXIS_THRESHOLD || Math.abs(axisY) > AXIS_THRESHOLD) {
            if (Math.abs(axisX) > Math.abs(axisY)) {
              if (axisX > AXIS_THRESHOLD) {
                callbacksRef.current.onNavigate?.('right');
                lastAxisNavTimeRef.current = now;
              } else if (axisX < -AXIS_THRESHOLD) {
                callbacksRef.current.onNavigate?.('left');
                lastAxisNavTimeRef.current = now;
              }
            } else {
              if (axisY > AXIS_THRESHOLD) {
                callbacksRef.current.onNavigate?.('down');
                lastAxisNavTimeRef.current = now;
              } else if (axisY < -AXIS_THRESHOLD) {
                callbacksRef.current.onNavigate?.('up');
                lastAxisNavTimeRef.current = now;
              }
            }
          }
        }

        // Action Buttons
        if (checkJustPressed(1)) callbacksRef.current.onButtonA?.();
        if (checkJustPressed(0)) callbacksRef.current.onButtonB?.();
        if (checkJustPressed(3)) callbacksRef.current.onButtonX?.();
        if (checkJustPressed(2)) callbacksRef.current.onButtonY?.();

        // Shoulders & Triggers
        if (checkJustPressed(4)) callbacksRef.current.onShoulderL?.();
        if (checkJustPressed(5)) callbacksRef.current.onShoulderR?.();
        if (checkJustPressed(6)) callbacksRef.current.onTriggerZL?.();
        if (checkJustPressed(7)) callbacksRef.current.onTriggerZR?.();

        // System Buttons
        if (checkJustPressed(9)) callbacksRef.current.onButtonPlus?.();
        if (checkJustPressed(8)) callbacksRef.current.onButtonMinus?.();
      }

      animFrameRef.current = requestAnimationFrame(pollGamepad);
    };

    animFrameRef.current = requestAnimationFrame(pollGamepad);

    return () => {
      if (animFrameRef.current !== null) {
        cancelAnimationFrame(animFrameRef.current);
      }
    };
  }, []);
}

export default useSwitchGamepad;
