let pieChartInstance = null;
let tempChartInstance = null;

async function updateDashboard() {
  try {
    const statusRes = await fetch(`${BASE_URL}/status`);
    const statusData = await statusRes.json();

    const histRes = await fetch(`${BASE_URL}/report/history`);
    const histData = await histRes.json();
    
    document.getElementById('connection-status').innerText = "🟢 System Online";
    document.getElementById('connection-status').style.background = "#28a745";

    let dhtTemp = 0; // Default if no data

    // Update the cards with the ESP32's data
    if (histData.length > 0) {
      const latest = histData[histData.length - 1].data;
      document.getElementById('liveFlame').innerText = latest.flameStatus || 4095;
      document.getElementById('liveGas').innerText = latest.mqValue || 0;
      document.getElementById('liveDist').innerText = (latest.distance || 999) + " cm";
      
      // Grab the DHT11 temperature from the robot!
      dhtTemp = latest.temp || 0; 
    }

    // Update Pie Chart
    const ctxPie = document.getElementById('pieChart').getContext('2d');
    if (pieChartInstance) pieChartInstance.destroy(); 
    
    let isFire = statusData.signal_high ? 1 : 0;
    let isSafe = statusData.signal_high ? 0 : 1;

    pieChartInstance = new Chart(ctxPie, {
      type: 'doughnut',
      data: {
        labels: ['Danger Detected', 'Safe'],
        datasets: [{ data: [isFire, isSafe], backgroundColor: ['#dc3545', '#28a745'] }]
      }
    });

    // Update Bar Chart (Now using DHT11)
    const ctxTemp = document.getElementById('tempChart').getContext('2d');
    if (tempChartInstance) tempChartInstance.destroy(); 

    tempChartInstance = new Chart(ctxTemp, {
      type: 'bar', 
      data: {
        labels: ['Ambient Temp (°C) [DHT11]'],
        datasets: [{
          label: 'Robot Environment Temperature',
          data: [dhtTemp], // Feed the DHT11 temp into the chart
          backgroundColor: '#ffc107',
          borderRadius: 4
        }]
      },
      options: { scales: { y: { beginAtZero: true, max: 80 } } } // Scaled to normal ambient ranges
    });

  } catch (error) { 
    console.error("Dashboard error:", error);
    document.getElementById('connection-status').innerText = "🔴 Disconnected";
    document.getElementById('connection-status').style.background = "#dc3545";
  }
}

setInterval(updateDashboard, 3000);
updateDashboard();