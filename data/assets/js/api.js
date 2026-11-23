(() => {
  async function request(url, options) {
    const response = await fetch(url, options || {});
    let payload = {};
    try { payload = await response.json(); } catch (_) {}
    if (!response.ok || payload.ok === false) {
      throw new Error(payload.error || ("Permintaan gagal (HTTP " + response.status + ")"));
    }
    return payload;
  }
  window.MHomeApi = {
    getConfig: () => request("/api/config"),
    getStatus: () => request("/api/status"),
    saveConfig: config => request("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(config)
    }),
    toggleChannel: (channel, state) => request("/api/toggle?ch=" + encodeURIComponent(channel) + "&state=" + (state ? "1" : "0"), { method: "POST" })
  };
})();
