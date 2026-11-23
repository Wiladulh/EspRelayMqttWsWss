(() => {
  let config = null;

  const byId = id => document.getElementById(id);
  const setValue = (id, value) => { byId(id).value = value == null ? "" : value; };
  const getValue = id => byId(id).value;

  function makeField(labelText, type, value, onChange, options) {
    const wrap = document.createElement("div");
    wrap.className = "field-group";
    const label = document.createElement("label");
    label.textContent = labelText;
    const input = document.createElement("input");
    input.type = type;
    if (options && options.min !== undefined) input.min = options.min;
    if (options && options.max !== undefined) input.max = options.max;
    if (options && options.step !== undefined) input.step = options.step;
    if (type === "checkbox") input.checked = Boolean(value);
    else input.value = value == null || Number(value) < 0 && type === "number" ? "" : value;
    input.addEventListener("change", () => onChange(type === "checkbox" ? input.checked : type === "number" ? (input.value === "" ? -1 : Number(input.value)) : input.value));
    label.appendChild(input);
    wrap.appendChild(label);
    return wrap;
  }

  function makeCheck(labelText, checked, onChange) {
    const label = document.createElement("label");
    label.className = "check-row";
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = Boolean(checked);
    input.addEventListener("change", () => onChange(input.checked));
    const text = document.createElement("span");
    text.textContent = labelText;
    label.append(input, text);
    return label;
  }

  function renderChannelSettings() {
    const root = byId("channelSettings");
    root.replaceChildren();
    (config.channels || []).forEach((channel, index) => {
      const card = document.createElement("article");
      card.className = "channel-setting-card";
      const header = document.createElement("div");
      header.className = "channel-setting-header";
      const title = document.createElement("strong");
      title.textContent = "Channel " + String(channel.id).padStart(2, "0");
      const state = document.createElement("span");
      state.textContent = channel.state ? "Status saat ini: ON" : "Status saat ini: OFF";
      header.append(title, state);

      const fields = document.createElement("div");
      fields.className = "channel-fields";
      fields.append(
        makeField("Nama channel", "text", channel.name, value => channel.name = value),
        makeField("Relay GPIO", "number", channel.rPin, value => channel.rPin = value, {min:-1,max:63,step:1}),
        makeField("Tombol GPIO", "number", channel.bPin, value => channel.bPin = value, {min:-1,max:63,step:1}),
        makeField("Jadwal ON", "time", String(channel.hOn).padStart(2,"0") + ":" + String(channel.mOn).padStart(2,"0"), value => {
          if (!value) return;
          const parts = value.split(":").map(Number);
          channel.hOn = parts[0]; channel.mOn = parts[1];
        }),
        makeField("Jadwal OFF", "time", String(channel.hOff).padStart(2,"0") + ":" + String(channel.mOff).padStart(2,"0"), value => {
          if (!value) return;
          const parts = value.split(":").map(Number);
          channel.hOff = parts[0]; channel.mOff = parts[1];
        })
      );
      const options = document.createElement("div");
      options.className = "channel-options";
      options.append(
        makeCheck("Relay active-low", channel.aLow, value => channel.aLow = value),
        makeCheck("Aktifkan jadwal", channel.sEn, value => channel.sEn = value)
      );
      card.append(header, fields, options);
      root.appendChild(card);
    });
  }

  function applyConfig(data) {
    config = data;
    [
      "deviceId","ssid1","ssid2","mqttHost","mqttPort","mqttPath","mqttUser",
      "mqttCaCert","tz"
    ].forEach(key => setValue(key, data[key]));
    ["pass1","pass2","mqttPass","apPassword"].forEach(key => {
      setValue(key, "");
      const isSet = Boolean(data[key + "Set"]);
      const node = byId(key);
      node.placeholder = isSet ? "Tersimpan — kosongkan untuk mempertahankan" : "Belum diatur";
    });
    byId("clearPass1").checked = false;
    byId("clearPass2").checked = false;
    byId("mqttUseSsl").checked = Boolean(data.mqttUseSsl);
    setValue("boardProfile", data.boardProfile || "Tidak diketahui");
    setValue("safeGpioPins", data.safeGpioPins || "Tidak tersedia");
    byId("pass1Status").textContent = data.pass1Set ? "Password utama tersimpan (disembunyikan)." : "Password utama belum diatur.";
    byId("pass2Status").textContent = data.pass2Set ? "Password cadangan tersimpan (disembunyikan)." : "Password cadangan belum diatur.";
    byId("apPasswordStatus").textContent = data.apPasswordSet ? "Password AP tersimpan (disembunyikan)." : "Password AP belum diatur.";
    renderChannelSettings();
  }

  async function load() {
    try {
      const data = await window.MHomeApi.getConfig();
      applyConfig(data);
      byId("saveMessage").textContent = "Konfigurasi berhasil dimuat.";
    } catch (error) {
      window.MHomeApp.toast("Gagal memuat pengaturan: " + error.message, true);
    }
  }

  function validate() {
    const id = getValue("deviceId");
    if (!/^[A-Za-z0-9_-]{1,26}$/.test(id)) throw new Error("Device ID harus 1–26 karakter: huruf, angka, - atau _.");
    const timezone = getValue("tz");
    if (timezone.trim() === "" || !Number.isInteger(Number(timezone)) || Number(timezone) < -12 || Number(timezone) > 14) {
      throw new Error("Timezone harus berupa bilangan bulat UTC-12 sampai UTC+14.");
    }
    if (getValue("ssid1").length > 32 || getValue("ssid2").length > 32) throw new Error("SSID maksimal 32 karakter.");
    for (const input of byId("channelSettings").querySelectorAll('input[type="time"]')) {
      if (!input.value) throw new Error("Waktu ON dan OFF pada setiap channel wajib diisi.");
    }
  }

  async function save(event) {
    event.preventDefault();
    const button = byId("saveSettings");
    try {
      validate();
      button.disabled = true;
      button.textContent = "Menyimpan…";
      config.deviceId = getValue("deviceId");
      config.ssid1 = getValue("ssid1");
      config.ssid2 = getValue("ssid2");
      config.pass1 = getValue("pass1");
      config.pass2 = getValue("pass2");
      config.clearPass1 = byId("clearPass1").checked;
      config.clearPass2 = byId("clearPass2").checked;
      config.mqttHost = getValue("mqttHost");
      config.mqttPort = Number(getValue("mqttPort"));
      config.mqttPath = getValue("mqttPath");
      config.mqttUser = getValue("mqttUser");
      config.mqttPass = getValue("mqttPass");
      config.mqttCaCert = getValue("mqttCaCert");
      config.apPassword = getValue("apPassword");
      config.mqttUseSsl = byId("mqttUseSsl").checked;
      config.tz = Number(getValue("tz"));
      await window.MHomeApi.saveConfig(config);
      byId("saveMessage").textContent = "Tersimpan. Perangkat sedang menerapkan konfigurasi.";
      window.MHomeApp.toast("Pengaturan berhasil disimpan.");
      await load();
      if (window.MHomeDashboard) window.MHomeDashboard.refresh();
    } catch (error) {
      byId("saveMessage").textContent = "Penyimpanan gagal.";
      window.MHomeApp.toast(error.message, true);
    } finally {
      button.disabled = false;
      button.textContent = "Simpan Pengaturan";
    }
  }

  function init() {
    byId("settingsForm").addEventListener("submit", save);
    load();
  }

  window.MHomeSettings = { init, load };
})();
