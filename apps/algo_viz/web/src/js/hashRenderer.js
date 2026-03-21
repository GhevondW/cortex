export function createHashRenderer(dom) {
    let slots = [];
    let highlights = new Set();

    function render() {
        const html = slots
            .map((slot, index) => {
                const empty = slot.state === 0;
                const deleted = slot.state === 2;
                const stateClass = empty ? "hash-slot-empty" : deleted ? "hash-slot-deleted" : "hash-slot-filled";
                const highlightClass = highlights.has(index) ? "hash-slot-highlight" : "";
                const value = empty ? "Empty" : deleted ? "Deleted" : String(slot.value);
                return `<div class="hash-slot ${stateClass} ${highlightClass}">
                    <div class="hash-slot-index">#${index}</div>
                    <div class="hash-slot-value">${value}</div>
                </div>`;
            })
            .join("");
        dom.hashContainer.innerHTML = html;
    }

    function setSlots(newSlots) {
        slots = newSlots;
        render();
    }

    function setSlot(index, state, value) {
        if (!slots[index]) return;
        slots[index] = { state, value };
        render();
    }

    function clearHighlights() {
        highlights = new Set();
        render();
    }

    function highlight(index) {
        highlights.add(index);
        render();
    }

    return { setSlots, setSlot, highlight, clearHighlights };
}
