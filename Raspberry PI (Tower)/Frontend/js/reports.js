let globalHistory = [];

async function loadReports() {
  try {
    const res = await fetch(`${BASE_URL}/report/history`);
    globalHistory = await res.json();
    const tbody = document.getElementById('reportsBody');
    tbody.innerHTML = ""; 

    globalHistory.slice().reverse().forEach(entry => {
      const row = `<tr>
        <td style="padding: 12px; border-bottom: 1px solid #eee;">${entry.ts}</td>
        <td style="padding: 12px; border-bottom: 1px solid #eee; font-weight: bold;">${entry.data.command || "STANDBY"}</td>
        <td style="padding: 12px; border-bottom: 1px solid #eee;">${entry.data.distance || 0}</td>
        <td style="padding: 12px; border-bottom: 1px solid #eee;">${entry.data.flameStatus || 4095}</td>
        <td style="padding: 12px; border-bottom: 1px solid #eee;">${entry.data.mqValue || 0}</td>
      </tr>`;
      tbody.innerHTML += row;
    });
  } catch (e) { console.error("Reports error:", e); }
}

function filterTable() {
  const input = document.getElementById("searchInput").value.toUpperCase();
  const tr = document.getElementById("reportsTable").getElementsByTagName("tr");
  for (let i = 1; i < tr.length; i++) {
    let textValue = tr[i].textContent || tr[i].innerText;
    tr[i].style.display = textValue.toUpperCase().indexOf(input) > -1 ? "" : "none";
  }
}

function downloadCSV() {
  if(globalHistory.length === 0) return alert("No data to download!");
  let csvContent = "data:text/csv;charset=utf-8,Timestamp,State,Distance,Flame,Gas\n";
  globalHistory.forEach(e => {
    let row = `${e.ts},${e.data.command||"N/A"},${e.data.distance||0},${e.data.flameStatus||4095},${e.data.mqValue||0}`;
    csvContent += row + "\n";
  });
  const link = document.createElement("a");
  link.setAttribute("href", encodeURI(csvContent));
  link.setAttribute("download", "Robot_Telemetry_Log.csv");
  document.body.appendChild(link);
  link.click();
}

setInterval(loadReports, 5000); 
loadReports();