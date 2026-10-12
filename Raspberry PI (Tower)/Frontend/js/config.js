const BASE_URL = "";



function initTheme() {

  if (localStorage.getItem('theme') === 'dark') {

    document.body.classList.add('dark-mode');

  }

}



function toggleTheme() {

  document.body.classList.toggle('dark-mode');

  if (document.body.classList.contains('dark-mode')) {

    localStorage.setItem('theme', 'dark');

  } else {

    localStorage.setItem('theme', 'light');

  }

}



// New Feature: Live System Clock for all pages

function updateClock() {

  const clockElement = document.getElementById('system-clock');

  if(clockElement) {

    const now = new Date();

    clockElement.innerText = now.toLocaleString();

  }

}

setInterval(updateClock, 1000);



initTheme();