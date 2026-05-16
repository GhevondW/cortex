// Continuously rotates an element via requestAnimationFrame. This is the
// "smoking gun" element on the page: if it stops rotating, the main thread
// is blocked. The blocking-apply demo deliberately triggers that freeze;
// the cooperative-apply path is the proof that the same workload doesn't.

export class SpinnerLiveness {
    constructor(element) {
        this._el = element;
        this._angle = 0;
        this._last = performance.now();
        this._rafId = 0;
    }

    start() {
        if (this._rafId !== 0) return;
        const tick = (now) => {
            const dt = now - this._last;
            this._last = now;
            // 1 full rotation per ~1.5s.
            this._angle = (this._angle + (dt / 1500) * 360) % 360;
            this._el.style.transform = `rotate(${this._angle.toFixed(1)}deg)`;
            this._rafId = window.requestAnimationFrame(tick);
        };
        this._last = performance.now();
        this._rafId = window.requestAnimationFrame(tick);
    }

    stop() {
        if (this._rafId !== 0) {
            window.cancelAnimationFrame(this._rafId);
            this._rafId = 0;
        }
    }
}
