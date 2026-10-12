let acknowledgedAlerts = new Set();

async function loadAlerts() {
  try {
    const res = await fetch(`${BASE_URL}/report/history`);
    const history = await res.json();
    const container = document.getElementById('alertsContainer');
    container.innerHTML = ""; 

    const criticalEvents = history.filter(entry => 
      (entry.data.flameStatus < 1000) || (entry.data.mqValue > 2000)
    );

    let displayCount = 0;

    criticalEvents.slice().reverse().forEach((entry, index) => {
      // Create a unique ID for the alert based on its timestamp
      const alertId = `alert_${entry.ts.replace(/\D/g,'')}`;
      if (acknowledgedAlerts.has(alertId)) return; // Skip if muted
      
      displayCount++;
      const isFire = entry.data.flameStatus < 1000;
      const reason = isFire ? "🔥 FIRE DETECTED (IR/YOLO)" : "☣️ HIGH GAS DETECTED (MQ Sensor)";
      const flashClass = index === 0 ? "flashing-card" : "";
      const borderColor = isFire ? "#dc3545" : "#ffc107";
      
      const card = `<div class="card ${flashClass}" style="background: #f8d7da; color: #721c24; border-left: 5px solid ${borderColor}; position: relative;">
        <strong style="font-size: 1.1em;">${reason}</strong>
        <button onclick="acknowledgeAlert('${alertId}')" style="position: absolute; right: 20px; top: 20px; background: transparent; border: 1px solid #721c24; color: #721c24; border-radius: 4px; padding: 4px 8px; cursor: pointer;">Acknowledge</button>
        <br>
        <span style="color: #555;">Logged at: ${entry.ts}</span><br>
        <hr style="border-color: #f5c6cb; margin: 8px 0;">
        Flame Raw Value: <b>${entry.data.flameStatus}</b> | Gas Raw Value: <b>${entry.data.mqValue}</b>
      </div>`;
      container.innerHTML += card;
    });

    if (displayCount === 0) {
      container.innerHTML = `<div class="card" style="background: #d4edda; color: #155724; border-left: 5px solid #28a745;">✅ System is stable. No unacknowledged anomalies detected.</div>`;
    }
  } catch (e) { console.error("Alerts error:", e); }
}

function acknowledgeAlert(id) {
  acknowledgedAlerts.add(id);
  loadAlerts(); // Refresh UI instantly
}

function clearAcknowledged() {
  acknowledgedAlerts.clear();
  loadAlerts();
}

setInterval(loadAlerts, 4000);
loadAlerts();