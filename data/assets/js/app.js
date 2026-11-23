(() => {
  let toastTimer = null;
  function toast(message, isError) {
    const node = document.getElementById("toast");
    node.textContent = message;
    node.className = "toast is-visible" + (isError ? " is-error" : "");
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { node.className = "toast"; }, 3200);
  }

  function showView(viewName) {
    const dashboard = viewName === "dashboard";
    document.getElementById("dashboardView").hidden = !dashboard;
    document.getElementById("settingsView").hidden = dashboard;
    document.getElementById("dashboardView").classList.toggle("is-visible", dashboard);
    document.getElementById("settingsView").classList.toggle("is-visible", !dashboard);
    document.querySelectorAll(".nav-tab").forEach(button => {
      const active = button.dataset.view === viewName;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-selected", active ? "true" : "false");
    });
  }

  function initSettingsNavigation() {
    document.querySelectorAll(".settings-nav-item").forEach(button => {
      button.addEventListener("click", () => {
        document.querySelectorAll(".settings-nav-item").forEach(item => item.classList.toggle("is-active", item === button));
        const section = document.getElementById(button.dataset.section);
        if (section) section.scrollIntoView({behavior:"smooth",block:"start"});
      });
    });
  }

  function init() {
    window.MHomeApp = { toast };
    document.querySelectorAll(".nav-tab").forEach(button => {
      button.addEventListener("click", () => showView(button.dataset.view));
    });
    initSettingsNavigation();
    window.MHomeDashboard.start();
    window.MHomeSettings.init();
  }

  document.addEventListener("DOMContentLoaded", init);
})();
