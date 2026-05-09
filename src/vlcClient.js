const DEFAULT_VOLUME_BASE = 256;
const DEFAULT_TIMEOUT_MS = 3500;

function getConfig() {
  return {
    protocol: process.env.VLC_PROTOCOL || "http",
    host: process.env.VLC_HOST || "127.0.0.1",
    port: process.env.VLC_PORT || "8080",
    password: process.env.VLC_PASSWORD || "vlc",
    timeoutMs: Number(process.env.VLC_TIMEOUT_MS || DEFAULT_TIMEOUT_MS),
    volumeStep: Number(process.env.VLC_VOLUME_STEP || 32)
  };
}

function authHeader(password) {
  return `Basic ${Buffer.from(`:${password}`).toString("base64")}`;
}

function commandUrl(config, command, params = {}) {
  const url = new URL(`${config.protocol}://${config.host}:${config.port}/requests/status.json`);

  if (command) {
    url.searchParams.set("command", command);
  }

  for (const [key, value] of Object.entries(params)) {
    if (value !== undefined && value !== null) {
      url.searchParams.set(key, String(value));
    }
  }

  return url;
}

async function requestStatus(command, params = {}) {
  const config = getConfig();
  const url = commandUrl(config, command, params);
  let response;

  try {
    response = await fetch(url, {
      headers: {
        Authorization: authHeader(config.password),
        Accept: "application/json"
      },
      signal: AbortSignal.timeout(config.timeoutMs)
    });
  } catch (error) {
    if (error.name === "AbortError" || error.name === "TimeoutError") {
      throw new Error(`Tempo esgotado ao conectar ao VLC em ${config.protocol}://${config.host}:${config.port}.`);
    }

    throw new Error(`Nao foi possivel conectar ao VLC em ${config.protocol}://${config.host}:${config.port}.`);
  }

  if (response.status === 401) {
    throw new Error("VLC recusou a senha. Confira VLC_PASSWORD e a senha Lua HTTP do VLC.");
  }

  if (!response.ok) {
    throw new Error(`VLC respondeu com HTTP ${response.status}.`);
  }

  return response.json();
}

export async function getVlcStatus() {
  return normalizeStatus(await requestStatus());
}

export async function sendControl(action, value) {
  const config = getConfig();
  const commands = {
    toggle: ["pl_pause"],
    pause: ["pl_forcepause"],
    resume: ["pl_forceresume"],
    volumeUp: ["volume", { val: `+${config.volumeStep}` }],
    volumeDown: ["volume", { val: `-${config.volumeStep}` }],
    mute: ["volume", { val: "0" }],
    subtitleOff: ["subtitle_track", { val: "-1" }]
  };

  if (action === "volumeSet") {
    const percent = clamp(Number(value), 0, 200);
    return normalizeStatus(await requestStatus("volume", { val: Math.round((percent / 100) * DEFAULT_VOLUME_BASE) }));
  }

  if (action === "audioTrack") {
    return normalizeStatus(await requestStatus("audio_track", { val: parseTrackValue(value) }));
  }

  if (action === "subtitleTrack") {
    return normalizeStatus(await requestStatus("subtitle_track", { val: parseTrackValue(value) }));
  }

  const command = commands[action];
  if (!command) {
    throw new Error(`Acao desconhecida: ${action}`);
  }

  return normalizeStatus(await requestStatus(command[0], command[1]));
}

function normalizeStatus(status) {
  const volumeRaw = Number(status.volume || 0);
  const length = Number(status.length || 0);
  const time = Number(status.time || 0);
  const meta = status.information?.category?.meta || {};

  return {
    state: status.state || "unknown",
    volumeRaw,
    volumePercent: clamp(Math.round((volumeRaw / DEFAULT_VOLUME_BASE) * 100), 0, 200),
    time,
    length,
    position: typeof status.position === "number" ? status.position : 0,
    title: getTitle(meta),
    artist: meta.artist || meta.Artist || "",
    filename: meta.filename || meta.Filename || "",
    tracks: extractTracks(status),
    updatedAt: new Date().toISOString()
  };
}

function extractTracks(status) {
  const categories = status.information?.category || {};
  const tracks = {
    audio: [],
    subtitles: []
  };

  for (const [name, stream] of Object.entries(categories)) {
    if (!isStreamCategory(name, stream)) {
      continue;
    }

    const id = Number((name.match(/\d+/) || [])[0]);
    if (!Number.isFinite(id)) {
      continue;
    }

    const typeText = normalizeText([
      readInsensitive(stream, "Type"),
      readInsensitive(stream, "Tipo"),
      readInsensitive(stream, "Stream Type"),
      ...Object.values(stream || {})
    ].join(" "));

    const track = {
      id,
      label: formatTrackLabel(name, stream)
    };

    if (typeText.includes("audio")) {
      tracks.audio.push(track);
    }

    if (
      typeText.includes("subtitle") ||
      typeText.includes("subtitulo") ||
      typeText.includes("legenda") ||
      typeText.includes("spu")
    ) {
      tracks.subtitles.push(track);
    }
  }

  tracks.audio.sort((a, b) => a.id - b.id);
  tracks.subtitles.sort((a, b) => a.id - b.id);
  return tracks;
}

function isStreamCategory(name, stream) {
  return /^stream\s+\d+/i.test(name) && stream && typeof stream === "object";
}

function readInsensitive(object, key) {
  if (!object) {
    return "";
  }

  const foundKey = Object.keys(object).find((item) => item.toLowerCase() === key.toLowerCase());
  return foundKey ? object[foundKey] : "";
}

function formatTrackLabel(name, stream) {
  const language = readInsensitive(stream, "Language") || readInsensitive(stream, "Idioma");
  const codec = readInsensitive(stream, "Codec");
  const description = readInsensitive(stream, "Description") || readInsensitive(stream, "Descricao");
  return [name.replace(/\s+/g, " "), language, description, codec]
    .filter(Boolean)
    .join(" - ");
}

function getTitle(meta) {
  return meta.title || meta.Title || meta.filename || meta.Filename || "VLC";
}

function parseTrackValue(value) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed)) {
    throw new Error("Faixa invalida.");
  }

  return parsed;
}

function clamp(value, min, max) {
  if (!Number.isFinite(value)) {
    return min;
  }

  return Math.min(Math.max(value, min), max);
}

function normalizeText(value) {
  return value
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase();
}
