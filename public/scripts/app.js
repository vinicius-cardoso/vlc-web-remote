const state = {
  connected: false,
  mediaKey: "",
  selectedAudioTrackId: null,
  selectedSubtitleTrackId: null,
  volumeTimer: null,
  lastStatus: null
};

const BLOCKS = 20;

const elements = {
  connectionDot: document.querySelector("[data-connection-dot]"),
  connectionText: document.querySelector("[data-connection-text]"),
  connectionChip: document.querySelector("[data-connection-chip]"),
  stateIcon: document.querySelector("[data-state-icon]"),
  stateLabel: document.querySelector("[data-state-label]"),
  playButton: document.querySelector(".play-key"),
  playIcon: document.querySelector("[data-play-icon]"),
  title: document.querySelector("[data-title]"),
  subtitle: document.querySelector("[data-subtitle]"),
  volumeCard: document.querySelector("[data-volume-card]"),
  volumeLabel: document.querySelector("[data-volume-label]"),
  volumeNote: document.querySelector("[data-volume-note]"),
  volumeBlocks: document.querySelector("[data-volume-blocks]"),
  volumeSlider: document.querySelector("[data-volume-slider]"),
  progressBlocks: document.querySelector("[data-progress-blocks]"),
  timeCurrent: document.querySelector("[data-time-current]"),
  timeTotal: document.querySelector("[data-time-total]"),
  timeLeft: document.querySelector("[data-time-left]"),
  audioTracks: document.querySelector("[data-audio-tracks]"),
  subtitleTracks: document.querySelector("[data-subtitle-tracks]"),
  refresh: document.querySelector("[data-refresh]")
};

// Pixel icons, drawn like the knob's: '#' = one pixel, rendered as crisp rects.
const icons = {
  play: [
    "##......",
    "####....",
    "######..",
    "########",
    "######..",
    "####....",
    "##......"
  ],
  "play-big": [
    "#.....",
    "##....",
    "###...",
    "####..",
    "#####.",
    "######",
    "#####.",
    "####..",
    "###...",
    "##....",
    "#....."
  ],
  pause: Array(7).fill("###..###"),
  stop: Array(7).fill("#######"),
  "seek-back": [
    "...#....#",
    "..##...##",
    ".###..###",
    "####.####",
    ".###..###",
    "..##...##",
    "...#....#"
  ],
  "seek-forward": [
    "#....#...",
    "##...##..",
    "###..###.",
    "####.####",
    "###..###.",
    "##...##..",
    "#....#..."
  ],
  refresh: [
    "..###.#.",
    ".#...##.",
    "#...###.",
    "#.......",
    "#.......",
    "#......#",
    ".#....#.",
    "..####.."
  ]
};

renderIcons();
buildBlocks(elements.progressBlocks);
buildBlocks(elements.volumeBlocks);
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

  elements.volumeBlocks.addEventListener("click", (event) => {
    const block = event.target.closest("i");
    if (!block || elements.volumeSlider.disabled) {
      return;
    }

    const value = ([...elements.volumeBlocks.children].indexOf(block) + 1) * (200 / BLOCKS);
    elements.volumeSlider.value = String(value);
    renderVolume(value);
    runAction("volumeSet", value);
  });

  elements.volumeSlider.addEventListener("input", () => {
    renderVolume(Number(elements.volumeSlider.value));
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
    rememberSelectedTrack(action, value);
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
  syncMedia(status);
  syncSelectedTracks(status.tracks);

  const playing = status.state === "playing";
  const paused = status.state === "paused";

  elements.title.textContent = status.title || "VLC";
  elements.subtitle.textContent = status.artist || status.filename || stateLabel(status.state);
  elements.playButton.setAttribute("aria-label", playing ? "Pausar" : "Reproduzir");
  setIcon(elements.playIcon, playing ? "pause" : "play-big");
  setPlayerState(status.state);

  elements.volumeSlider.value = String(status.volumePercent);
  renderVolume(status.volumePercent);

  const filled = status.length > 0 ? Math.floor((status.time / status.length) * BLOCKS) : 0;
  fillBlocks(elements.progressBlocks, filled);
  elements.timeCurrent.textContent = formatTime(status.time);
  elements.timeTotal.textContent = formatTime(status.length);
  elements.timeLeft.textContent = status.length > 0 ? `-${formatTime(status.length - status.time)}` : "";

  renderTracks(elements.audioTracks, "audioTrack", status.tracks.audio, "Nenhum áudio informado pelo VLC.", state.selectedAudioTrackId);
  renderTracks(elements.subtitleTracks, "subtitleTrack", withSubtitleOff(status.tracks.subtitles), "Nenhuma legenda informada pelo VLC.", state.selectedSubtitleTrackId);

  if (!playing && !paused && status.state) {
    elements.subtitle.textContent = stateLabel(status.state);
  }
}

function syncMedia(status) {
  const mediaKey = [status.title, status.filename, status.length].filter(Boolean).join("|");
  if (state.mediaKey && mediaKey && state.mediaKey !== mediaKey) {
    state.selectedAudioTrackId = null;
    state.selectedSubtitleTrackId = null;
  }

  state.mediaKey = mediaKey;
}

function syncSelectedTracks(tracks) {
  const activeAudioId = normalizeTrackId(tracks?.activeAudioId);
  const activeSubtitleId = normalizeTrackId(tracks?.activeSubtitleId);

  if (activeAudioId !== null) {
    state.selectedAudioTrackId = activeAudioId;
  }

  if (activeSubtitleId !== null) {
    state.selectedSubtitleTrackId = activeSubtitleId;
  }
}

function rememberSelectedTrack(action, value) {
  const trackId = normalizeTrackId(value);
  if (trackId === null) {
    return;
  }

  if (action === "audioTrack") {
    state.selectedAudioTrackId = trackId;
  }

  if (action === "subtitleTrack") {
    state.selectedSubtitleTrackId = trackId;
  }
}

function setConnection(connected, message = "") {
  state.connected = connected;
  elements.connectionDot.classList.toggle("connected", connected);
  elements.connectionDot.classList.toggle("error", !connected);
  elements.connectionText.textContent = connected ? "Conectado ao VLC" : message || "Sem conexao com o VLC";
  elements.connectionChip.textContent = connected ? "On" : "Off";
  elements.connectionChip.classList.toggle("on", connected);
  elements.connectionChip.classList.toggle("off", !connected);

  if (!connected) {
    setPlayerState("offline");
  }
}

function setPlayerState(value) {
  const labels = {
    playing: "Tocando",
    paused: "Pausado",
    stopped: "Parado",
    offline: "Sem VLC"
  };
  elements.stateLabel.textContent = labels[value] || "Parado";
  setIcon(elements.stateIcon, value === "playing" ? "play" : value === "paused" ? "pause" : "stop");
  elements.stateIcon.classList.toggle("playing", value === "playing");
  elements.stateIcon.classList.toggle("paused", value === "paused");
}

// Volume card: big percent and blocks in the level color (green to 100%,
// yellow boost, red above 150%), like the knob.
function renderVolume(percent) {
  const value = Math.round(Number(percent) || 0);
  const level = value === 0 ? "mute" : value > 150 ? "loud" : value > 100 ? "boost" : "ok";
  elements.volumeCard.dataset.level = level;
  elements.volumeLabel.textContent = `${value}%`;
  elements.volumeNote.textContent = level === "mute" ? "Mudo" : level === "ok" ? "" : "Boost";
  fillBlocks(elements.volumeBlocks, Math.round(value / (200 / BLOCKS)));
}

function buildBlocks(container) {
  container.replaceChildren(...Array.from({ length: BLOCKS }, () => document.createElement("i")));
}

function fillBlocks(container, count) {
  [...container.children].forEach((block, index) => {
    block.classList.toggle("on", index < count);
  });
}

function setBusy(isBusy) {
  document.querySelectorAll("button, input").forEach((control) => {
    control.disabled = isBusy;
  });
}

function renderTracks(container, action, tracks, emptyText, selectedId = null) {
  const items = tracks || [];
  container.replaceChildren();

  if (!items.length) {
    const empty = document.createElement("p");
    empty.className = "track-empty";
    empty.textContent = emptyText;
    container.append(empty);
    return;
  }

  for (const item of items) {
    const track = normalizeTrack(item);
    const button = document.createElement("button");
    const selected = track.selected === true || (selectedId !== null && track.id === selectedId);
    button.className = `track-button${track.wide ? " wide" : ""}${selected ? " selected" : ""}`;
    button.type = "button";
    button.dataset.action = action;
    button.dataset.value = track.id;
    button.setAttribute("aria-pressed", selected ? "true" : "false");
    button.textContent = track.title;

    if (track.detail) {
      const detail = document.createElement("span");
      detail.textContent = track.detail;
      button.append(detail);
    }

    container.append(button);
  }
}

function normalizeTrackId(value) {
  const trackId = Number(value);
  return Number.isInteger(trackId) ? trackId : null;
}

function renderDefaultTracks() {
  renderTracks(elements.audioTracks, "audioTrack", [], "Aguardando VLC.");
  renderTracks(elements.subtitleTracks, "subtitleTrack", [], "Aguardando VLC.");
}

function withSubtitleOff(tracks) {
  return [{ id: -1, title: "Sem legenda", wide: true }, ...(tracks || [])];
}

function normalizeTrack(track) {
  if (track.title) {
    return {
      ...track,
      id: normalizeTrackId(track.id) ?? track.id
    };
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
  const rows = icons[name];
  if (!rows) {
    target.innerHTML = "";
    return;
  }

  const rects = [];
  rows.forEach((row, y) => {
    for (const match of row.matchAll(/#+/g)) {
      rects.push(`<rect x="${match.index}" y="${y}" width="${match[0].length}" height="1"></rect>`);
    }
  });
  target.innerHTML = `<svg viewBox="0 0 ${rows[0].length} ${rows.length}">${rects.join("")}</svg>`;
}
