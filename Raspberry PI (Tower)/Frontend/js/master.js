// Add a slight delay to ensure the DOM is loaded
window.addEventListener('DOMContentLoaded', () => {
  const videoFeed = document.getElementById('videoFeed');
  if(videoFeed) videoFeed.src = `${BASE_URL}/video`;
});

function getDirectionText(degrees) {
  const dirs = ["North", "North-East", "East", "South-East", "South", "South-West", "West", "North-West"];
  return dirs[Math.round(degrees / 45) % 8];
}

async function updateCameraDirection() {
  try {
    const res = await fetch(`${BASE_URL}/master`); 
    if(!res.ok) return;
    const data = await res.json();

    if (data && data.heading !== undefined) {
      const dirText = getDirectionText(data.heading);
      
      const headingEl = document.getElementById('headingValue');
      const servoEl = document.getElementById('servoValue');
      const needleEl = document.getElementById('compassNeedle');

      // Safely update elements only if they exist on the page
      if(headingEl) headingEl.innerText = `${data.heading.toFixed(1)}° (${dirText})`;
      if(servoEl) servoEl.innerText = `${data.servoAngle}°`;
      if(needleEl) needleEl.style.transform = `rotate(${data.heading}deg)`;
    }
  } catch (e) { 
    console.warn("Waiting for Master telemetry..."); 
  }
}

async function forceAlert(state) {
  try {
    await fetch(`${BASE_URL}/override/${state}`, { method: 'POST' });
    alert(`System override: ${state.toUpperCase()}`);
  } catch (e) { 
    alert("Failed to send command. Ensure server is running."); 
  }
}

setInterval(updateCameraDirection, 1000);
updateCameraDirection();