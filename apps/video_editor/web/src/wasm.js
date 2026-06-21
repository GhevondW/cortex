// Loads the Emscripten-generated runtime script (`video_editor.js`) and
// resolves to the initialized Module object. Identical pattern to algo_viz.

export function loadWasmRuntime() {
    return new Promise((resolve, reject) => {
        const runtimeUrl = "./video_editor.js";
        const runtimeHref = new URL(runtimeUrl, window.location.href).href;
        const moduleObj = {};
        moduleObj.locateFile = (path, scriptDirectory) => {
            const base = scriptDirectory || runtimeHref;
            return new URL(path, base).href;
        };
        moduleObj.onAbort = (msg) => reject(new Error(msg || "WASM aborted"));
        moduleObj.onRuntimeInitialized = () => resolve(moduleObj);
        window.Module = moduleObj;

        const script = document.createElement("script");
        script.src = runtimeUrl;
        script.async = true;
        script.onerror = () => reject(new Error(`Failed to load ${runtimeUrl}`));
        document.head.appendChild(script);
    });
}
