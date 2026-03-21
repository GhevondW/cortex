function required(id) {
    const element = document.getElementById(id);
    if (!element) {
        throw new Error(`Missing required DOM element: #${id}`);
    }
    return element;
}

export function getDom() {
    return {
        canvas: required("treeCanvas"),
        canvasContainer: required("canvasContainer"),
        emptyMsg: required("emptyMsg"),
        traversalOutput: required("traversalOutput"),
        statusText: required("statusText"),
        comparisonsEl: required("comparisonsEl"),
        stepsEl: required("stepsEl"),
        algorithmSelect: required("algorithmSelect"),
        valueInput: required("valueInput"),
        valueInputGroup: required("valueInputGroup"),
        runBtn: required("runBtn"),
        stepBtn: required("stepBtn"),
        pauseBtn: required("pauseBtn"),
        resetBtn: required("resetBtn"),
        speedSlider: required("speedSlider"),
        speedLabel: required("speedLabel"),
        presetSelect: required("presetSelect"),
        loadPresetBtn: required("loadPresetBtn"),
        addValueInput: required("addValueInput"),
        addNodeBtn: required("addNodeBtn"),
        randomBtn: required("randomBtn"),
        clearBtn: required("clearBtn"),
        zoomInBtn: required("zoomInBtn"),
        zoomOutBtn: required("zoomOutBtn"),
        fitBtn: required("fitBtn"),
        spreadHBtn: required("spreadHBtn"),
        shrinkHBtn: required("shrinkHBtn"),
        spreadVBtn: required("spreadVBtn"),
        shrinkVBtn: required("shrinkVBtn"),
    };
}
