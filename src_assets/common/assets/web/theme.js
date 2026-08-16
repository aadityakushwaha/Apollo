// Altagones: single committed dark console look — the design is locked.
export const getPreferredTheme = () => 'dark'

const setTheme = () => {
    document.documentElement.setAttribute('data-bs-theme', 'dark')
}

export const showActiveTheme = () => {}

export function setupThemeToggleListener() {
    setTheme()
}

export function loadAutoTheme() {
    setTheme()
}
