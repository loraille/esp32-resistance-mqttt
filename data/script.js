async function fetchStatus() {
    const res = await fetch("/api/status");
    const data = await res.json();
    document.getElementById("temp").textContent = Math.ceil(data.temperature, 1) + " °C";
    document.getElementById("status").textContent = data.status;
    const controls = document.getElementById("controls");
    if (data.canControl) {
      controls.innerHTML = `
        <button onclick="sendCommand('${data.relay ? 'OFF' : 'ON'}')">
          ${data.relay ? "❌ Éteindre" : "🔥 Allumer"}
        </button>`;
    } else {
      controls.innerHTML = `<p class="safety">Commande bloquée</p>`;
    }
  }
  
  async function sendCommand(cmd) {
    await fetch("/api/relay", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ command: cmd })
    });
    fetchStatus();
  }
  
  async function loadConfig() {
    const res = await fetch("/api/config");
    const cfg = await res.json();
    document.getElementById("start").value = cfg.active_start_hour;
    document.getElementById("end").value = cfg.active_end_hour;
    document.getElementById("tmax").value = cfg.temp_max;
    document.getElementById("treset").value = cfg.temp_reset;
  }
  
  document.addEventListener("DOMContentLoaded", () => {
    if (document.getElementById("configForm")) {
      loadConfig();
      document.getElementById("configForm").addEventListener("submit", async (e) => {
        e.preventDefault();
        const cfg = {
          active_start_hour: parseInt(document.getElementById("start").value),
          active_end_hour: parseInt(document.getElementById("end").value),
          temp_max: parseFloat(document.getElementById("tmax").value),
          temp_reset: parseFloat(document.getElementById("treset").value)
        };
        await fetch("/api/config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(cfg)
        });
        alert("Config sauvegardée !");
      });
    } else {
      fetchStatus();
      setInterval(fetchStatus, 5000);
    }
  });
  