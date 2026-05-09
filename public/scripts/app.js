const state = {
  connected: false,
  volumeTimer: null,
  lastStatus: null
};

const elements = {
  connectionDot: document.querySelector("[data-connection-dot]"),
  connectionText: document.querySelector("[data-connection-text]"),
  title: document.querySelector("[data-title]"),
  subtitle: document.querySelector("[data-subtitle]"),
  playLabel: document.querySelector("[data-play-label]"),
  volumeLabel: document.querySelector("[data-volume-label]"),
  volumeSlider: document.querySelector("[data-volume-slider]"),
  progressFill: document.querySelector("[data-progress-fill]"),
  timeCurrent: document.querySelector("[data-time-current]"),
  timeTotal: document.querySelector("[data-time-total]"),
  audioTracks: document.querySelector("[data-audio-tracks]"),
  subtitleTracks: document.querySelector("[data-subtitle-tracks]"),
  refresh: document.querySelector("[data-refresh]")
};

const icons = {
  refresh: '<svg viewBox="0 0 24 24"><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"></path><path d="M3 21v-5h5"></path><path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"></path><path d="M16 8h5V3"></path></svg>',
  "play-pause": '<svg viewBox="0 0 24 24"><path d="m8 5 11 7-11 7V5Z"></path><path d="M4 5v14"></path></svg>',
  pause: '<svg viewBox="0 0 24 24"><path d="M8 5v14"></path><path d="M16 5v14"></path></svg>',
  play: '<svg viewBox="0 0 24 24"><path d="m6 3 14 9-14 9V3Z"></path></svg>',
  "volume-down": '<svg viewBox="0 0 24 24"><path d="M11 5 6 9H2v6h4l5 4V5Z"></path><path d="M15.54 8.46a5 5 0 0 1 0 7.07"></path></svg>',
  "volume-up": '<svg viewBox="0 0 24 24"><path d="M11 5 6 9H2v6h4l5 4V5Z"></path><path d="M15.54 8.46a5 5 0 0 1 0 7.07"></path><path d="M19.07 4.93a10 10 0 0 1 0 14.14"></path></svg>'
};

renderIcons();
bindEvents();
renderDefaultTracks();
refreshStatus();
window.setInterval(refreshStatus, 3000);

function bindEvents() {
  document.addEventListener("click", async (event) => {
    const button = event.target.closest("[data-action]");
    if (!button) {
      return;
    }

    await runAction(button.dataset.action, button.dataset.value);
  });

  elements.refresh.addEventListener("click", refreshStatus);

  elements.volumeSlider.addEventListener("input", () => {
    elements.volumeLabel.textContent = `${elements.volumeSlider.value}%`;
    window.clearTimeout(state.volumeTimer);
    state.volumeTimer = window.setTimeout(() => {
      runAction("volumeSet", elements.volumeSlider.value);
    }, 180);
  });
}

async function refreshStatus() {
  try {
    const payload = await fetchJson("/api/status");
    setStatus(payload.status);
    setConnection(true);
  } catch (error) {
    setConnection(false, error.message);
    if (!state.lastStatus) {
      renderDefaultTracks();
    }
  }
}

async function runAction(action, value) {
  setBusy(true);

  try {
    const payload = await fetchJson("/api/control", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action, value })
    });
    setStatus(payload.status);
    setConnection(true);
    if (navigator.vibrate) {
      navigator.vibrate(12);
    }
  } catch (error) {
    setConnection(false, error.message);
  } finally {
    setBusy(false);
  }
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  const payload = await response.json().catch(() => ({}));

  if (!response.ok || payload.ok === false) {
    throw new Error(payload.error || `Erro HTTP ${response.status}`);
  }

  return payload;
}

function setStatus(status) {
  state.lastStatus = status;
  const playing = status.state === "playing";
  const paused = status.state === "paused";

  elements.title.textContent = status.title || "VLC";
  elements.subtitle.textContent = status.artist || status.filename || stateLabel(status.state);
  elements.playLabel.textContent = playing ? "Pausar" : "Reproduzir";
  setIcon(document.querySelector(".play-button .icon"), playing ? "pause" : "play");

  elements.volumeLabel.textContent = `${status.volumePercent}%`;
  elements.volumeSlider.value = String(status.volumePercent);

  const progress = status.length > 0 ? Math.round((status.time / status.length) * 100) : 0;
  elements.progressFill.style.width = `${Math.min(Math.max(progress, 0), 100)}%`;
  elements.timeCurrent.textContent = formatTime(status.time);
  elements.timeTotal.textContent = formatTime(status.length);

  renderTracks(elements.audioTracks, "audioTrack", status.tracks.audio, fallbackAudioTracks());
  renderTracks(elements.subtitleTracks, "subtitleTrack", withSubtitleOff(status.tracks.subtitles), fallbackSubtitleTracks());

  if (!playing && !paused && status.state) {
    elements.subtitle.textContent = stateLabel(status.state);
  }
}

function setConnection(connected, message = "") {
  state.connected = connected;
  elements.connectionDot.classList.toggle("connected", connected);
  elements.connectionDot.classList.toggle("error", !connected);
  elements.connectionText.textContent = connected ? "Conectado ao VLC" : message || "Sem conexao com o VLC";
}

function setBusy(isBusy) {
  document.querySelectorAll("button, input").forEach((control) => {
    control.disabled = isBusy;
  });
}

function renderTracks(container, action, tracks, fallbackTracks) {
  const items = tracks && tracks.length ? tracks : fallbackTracks;
  container.replaceChildren();

  for (const item of items) {
    const track = normalizeTrack(item);
    const button = document.createElement("button");
    button.className = `track-button${track.wide ? " wide" : ""}`;
    button.type = "button";
    button.dataset.action = action;
    button.dataset.value = track.id;
    button.textContent = track.title;

    if (track.detail) {
      const detail = document.createElement("span");
      detail.textContent = track.detail;
      button.append(detail);
    }

    container.append(button);
  }
}

function renderDefaultTracks() {
  renderTracks(elements.audioTracks, "audioTrack", [], fallbackAudioTracks());
  renderTracks(elements.subtitleTracks, "subtitleTrack", [], fallbackSubtitleTracks());
}

function withSubtitleOff(tracks) {
  if (!tracks || !tracks.length) {
    return [];
  }

  return [{ id: -1, title: "Sem legenda", wide: true }, ...tracks];
}

function fallbackAudioTracks() {
  return [
    { id: 1, title: "Áudio 1" },
    { id: 2, title: "Áudio 2" },
    { id: 3, title: "Áudio 3" }
  ];
}

function fallbackSubtitleTracks() {
  return [
    { id: -1, title: "Sem legenda", wide: true },
    { id: 1, title: "Legenda 1" },
    { id: 2, title: "Legenda 2" },
    { id: 3, title: "Legenda 3" }
  ];
}

function normalizeTrack(track) {
  if (track.title) {
    return track;
  }

  const parts = String(track.label || `Faixa ${track.id}`).split(" - ");
  return {
    id: track.id,
    title: parts[0] || `Faixa ${track.id}`,
    detail: parts.slice(1).join(" - ")
  };
}

function stateLabel(value) {
  const labels = {
    playing: "Reproduzindo",
    paused: "Pausado",
    stopped: "Parado",
    unknown: "Status indisponivel"
  };
  return labels[value] || value;
}

function formatTime(seconds) {
  const safeSeconds = Number.isFinite(seconds) ? Math.max(seconds, 0) : 0;
  const minutes = Math.floor(safeSeconds / 60);
  const remainingSeconds = Math.floor(safeSeconds % 60);
  return `${String(minutes).padStart(2, "0")}:${String(remainingSeconds).padStart(2, "0")}`;
}

function renderIcons() {
  document.querySelectorAll("[data-icon]").forEach((target) => {
    setIcon(target, target.dataset.icon);
  });
}

function setIcon(target, name) {
  target.innerHTML = icons[name] || "";
}
