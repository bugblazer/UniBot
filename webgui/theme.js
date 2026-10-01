// Applies the saved theme before first paint (loaded in <head>) and wires the toggle button.
(function () {
  const KEY = 'unibot-theme';
  const root = document.documentElement;

  function read() {
    try {
      return localStorage.getItem(KEY);
    } catch {
      return null;
    }
  }

  function apply(theme) {
    root.dataset.theme = theme;
    try {
      localStorage.setItem(KEY, theme);
    } catch {
      // Storage can be unavailable (private mode); the theme still applies for this page.
    }
  }

  root.dataset.theme = read() === 'dark' ? 'dark' : 'light';

  document.addEventListener('DOMContentLoaded', () => {
    const toggle = document.getElementById('themeToggle');
    if (!toggle) return;
    const label = () => (root.dataset.theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode');
    toggle.setAttribute('aria-label', label());
    toggle.addEventListener('click', () => {
      apply(root.dataset.theme === 'dark' ? 'light' : 'dark');
      toggle.setAttribute('aria-label', label());
    });
  });
})();
