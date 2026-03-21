export function createState() {
    return {
        treeNodes: {},
        rootId: -1,
        nodePositions: {},
        nodeHighlights: {},
        edgeHighlights: {},
        isRunning: false,
        isPaused: false,
        isReady: false,
        playTimer: null,
        stepCount: 0,
        comparisons: 0,
        currentSpeed: 300,
    };
}
