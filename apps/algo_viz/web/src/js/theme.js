const STORAGE_KEY = "algoviz-theme";
const THEME_DARK = "dark";
const THEME_LIGHT = "light";

function isValidTheme(value) {
    return value === THEME_DARK || value === THEME_LIGHT;
}

function getStoredTheme() {
    try {
        const saved = localStorage.getItem(STORAGE_KEY);
        return isValidTheme(saved) ? saved : null;
    } catch {
        return null;
    }
}

function getCurrentTheme() {
    const fromDom = document.documentElement.dataset.theme;
    if (isValidTheme(fromDom)) return fromDom;
    return getStoredTheme() || THEME_DARK;
}

function saveTheme(theme) {
    try {
        localStorage.setItem(STORAGE_KEY, theme);
    } catch {
        /* no-op */
    }
}

function updateToggleButtons(theme) {
    const buttons = document.querySelectorAll("[data-theme-toggle]");
    const nextLabel = theme === THEME_DARK ? "Light Mode" : "Dark Mode";
    for (const button of buttons) {
        button.textContent = nextLabel;
        button.setAttribute("aria-label", `Switch to ${nextLabel}`);
    }
}

function applyTheme(theme) {
    const resolved = isValidTheme(theme) ? theme : THEME_DARK;
    document.documentElement.dataset.theme = resolved;
    saveTheme(resolved);
    updateToggleButtons(resolved);
}

function toggleTheme() {
    const next = getCurrentTheme() === THEME_DARK ? THEME_LIGHT : THEME_DARK;
    applyTheme(next);
}

function initThemeToggle() {
    const buttons = document.querySelectorAll("[data-theme-toggle]");
    if (buttons.length === 0) return;

    applyTheme(getCurrentTheme());
    for (const button of buttons) {
        button.addEventListener("click", toggleTheme);
    }
}

initThemeToggle();
