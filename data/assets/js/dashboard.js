(() => {
  let refreshTimer = null;
  let loading = false;

  function setText(id, value) {
    const node = document.getElementById(id);
    if (node) node.textContent = value;
  }

  function setConnectionBadge(wifiConnected, apActive) {
    const badge = document.getElementById("connectionBadge");
    const reachable = wifiConnected || apActive;
    badge.className = "status-badge " + (reachable ? "status-connected" : "status-disconnected");
    badge.querySelector("span").textContent = wifiConnected ? "Perangkat online" : apActive ? "Fallback AP aktif" : "Perangkat tidak terjangkau";
  }

  function renderStatus(status) {
    const wifi = Boolean(status.wifiConnected);
    const mqtt = Boolean(status.mqttConnected);
    const ap = Boolean(status.apActive);
    setConnectionBadge(wifi, ap);
    setText("wifiState", wifi ? "Terhubung" : "Terputus");
    setText("wifiDetail", wifi ? ((status.wifiSsid || "Wi-Fi") + " · " + (status.wifiIp || "IP tidak tersedia")) : "Menggunakan fallback jika tersedia");
    setText("mqttState", mqtt ? "Terhubung" : "Terputus");
    setText("mqttDetail", mqtt ? "Broker MQTT over WebSocket aktif" : "Menunggu koneksi broker");
    setText("apState", ap ? "Aktif" : "Nonaktif");
    setText("apDetail", ap ? ("IP " + (status.apIp || "192.168.4.1")) : "AP dimatikan saat Wi-Fi tersambung");
    setText("deviceLabel", status.deviceId || "M-Home device");
    renderChannels(status.channels || []);
  }

  function renderChannels(channels) {
    const grid = document.getElementById("channelGrid");
    const active = channels.filter(channel => Boolean(channel.state)).length;
    setText("relaySummary", active + " / " + channels.length + " aktif");
    grid.replaceChildren();
    if (!channels.length) {
      const empty = document.createElement("div");
      empty.className = "loading-panel";
      empty.textContent = "Belum ada channel yang tersedia.";
      grid.appendChild(empty);
      return;
    }
    channels.forEach(channel => {
      const card = document.createElement("article");
      card.className = "channel-card" + (channel.state ? " is-on" : "");
      const info = document.createElement("div");
      info.className = "channel-info";
      const kicker = document.createElement("span");
      kicker.className = "channel-kicker";
      kicker.textContent = "CHANNEL " + String(channel.id).padStart(2, "0");
      const name = document.createElement("strong");
      name.textContent = channel.name || ("Relay " + channel.id);
      const state = document.createElement("span");
      state.className = "channel-state";
      state.textContent = channel.state ? "ON · Relay aktif" : "OFF · Relay nonaktif";
      info.append(kicker, name, state);

      const label = document.createElement("label");
      label.className = "toggle-control";
      label.setAttribute("aria-label", "Kontrol " + (channel.name || ("Relay " + channel.id)));
      const input = document.createElement("input");
      input.type = "checkbox";
      input.checked = Boolean(channel.state);
      input.addEventListener("change", async () => {
        input.disabled = true;
        try {
          await window.MHomeApi.toggleChannel(channel.id, input.checked);
          await refresh();
        } catch (error) {
          input.checked = !input.checked;
          window.MHomeApp.toast(error.message, true);
        } finally {
          input.disabled = false;
        }
      });
      const track = document.createElement("span");
      track.className = "toggle-track";
      label.append(input, track);
      card.append(info, label);
      grid.appendChild(card);
    });
  }

  async function refresh() {
    if (loading) return;
    loading = true;
    try {
      const status = await window.MHomeApi.getStatus();
      renderStatus(status);
    } catch (error) {
      setConnectionBadge(false);
      setText("wifiState", "Tidak tersedia");
      setText("mqttState", "Tidak tersedia");
      window.MHomeApp.toast(error.message, true);
    } finally {
      loading = false;
    }
  }

  function start() {
    const button = document.getElementById("refreshDashboard");
    button.addEventListener("click", refresh);
    refresh();
    if (refreshTimer) clearInterval(refreshTimer);
    refreshTimer = setInterval(refresh, 5000);
  }

  window.MHomeDashboard = { start, refresh };
})();
