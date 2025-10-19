async function fetchStatus() {
    const res = await fetch("/api/status");
    const data = await res.json();
    document.getElementById("temp").textContent = Math.ceil(data.temperature, 1) + " °C";
    document.getElementById("status").textContent = data.status;

    // Afficher la version
    if (data.version) {
      document.getElementById("version").textContent = "v" + data.version;
    }

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

  async function fetchLogs() {
    const res = await fetch("/api/logs");
    const data = await res.json();
    const logsDiv = document.getElementById("mqtt-logs");

    if (data.logs && data.logs.length > 0) {
      logsDiv.innerHTML = data.logs.map(log => {
        // Colorer les différents types de messages
        let className = "";
        if (log.includes("←")) className = "in";          // Messages MQTT entrants
        else if (log.includes("→")) className = "out";    // Messages MQTT sortants
        else if (log.includes("✅")) className = "success"; // Succès
        else if (log.includes("❌") || log.includes("🛑")) className = "error"; // Erreurs
        else if (log.includes("⚠️")) className = "warning"; // Warnings
        else if (log.includes("🌡️")) className = "temp";    // Température

        return `<div class="${className}">${log}</div>`;
      }).join("");

      // Auto-scroll vers le bas
      logsDiv.scrollTop = logsDiv.scrollHeight;
    } else {
      logsDiv.innerHTML = '<div style="color: #888;">Aucun log disponible</div>';
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
        const res = await fetch("/api/config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(cfg)
        });
        const data = await res.json();
        if (data.reboot) {
          alert("⚙️ Configuration sauvegardée !\n\nL'ESP32 va redémarrer dans 2 secondes...");
          setTimeout(() => {
            window.location.href = "/";
          }, 3000);
        } else {
          alert("Config sauvegardée !");
        }
      });
    } else {
      // Page principale
      fetchStatus();
      fetchLogs();
      setInterval(fetchStatus, 5000);
      setInterval(fetchLogs, 3000);  // Rafraîchir les logs plus fréquemment
    }
  });
  