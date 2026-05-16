// Renders a frame's RGBA buffer onto a <canvas>. Constructs an ImageData
// from the Uint8ClampedArray view once per draw — cheap; this is the
// preview path that runs on every slider change and every animation frame.

export class CanvasRenderer {
    constructor(canvasEl) {
        this._canvas = canvasEl;
        this._ctx = canvasEl.getContext("2d", { willReadFrequently: false });
    }

    resize(width, height) {
        this._canvas.width = width;
        this._canvas.height = height;
    }

    draw(pixels, width, height) {
        if (!pixels) return;
        const img = new ImageData(new Uint8ClampedArray(pixels), width, height);
        this._ctx.putImageData(img, 0, 0);
    }
}
