document.addEventListener('DOMContentLoaded', () => {
  const navLinks = document.querySelectorAll('a[href^="https://jitpack.io/"]');
  navLinks.forEach((link) => {
    link.setAttribute('target', '_blank');
    link.setAttribute('rel', 'noopener');
  });
});
