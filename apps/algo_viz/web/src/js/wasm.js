export function loadWasmRuntime() {
    return new Promise((resolve, reject) => {
        const runtimeUrl = window.__ALGOVIZ_RUNTIME_URL__ || "./algoviz_bst.js";
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
