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
    if (!stream || typeof stream !== "object") {
      continue;
    }

    const kind = getTrackKind(stream);
    if (!kind) {
      continue;
    }

    const id = getTrackId(name, stream);
    if (!Number.isInteger(id)) {
      continue;
    }

    const target = kind === "audio" ? tracks.audio : tracks.subtitles;
    target.push(formatTrack(kind, id, target.length, name, stream));
  }

  for (const group of [tracks.audio, tracks.subtitles]) {
    group.sort((a, b) => a.id - b.id);
    for (let index = 0; index < group.length; index += 1) {
      if (!group[index].title) {
        group[index].title = fallbackTrackTitle(group[index].kind, index);
      }
    }
  }

  return tracks;
}

function getTrackKind(stream) {
  const typeText = normalizeText([
    readInsensitive(stream, "Type"),
    readInsensitive(stream, "Tipo"),
    readInsensitive(stream, "Stream Type"),
    readInsensitive(stream, "Tipo de fluxo"),
    readInsensitive(stream, "Tipo da faixa")
  ].join(" "));

  if (typeText.includes("audio")) {
    return "audio";
  }

  if (
    typeText.includes("subtitle") ||
    typeText.includes("subtitulo") ||
    typeText.includes("legenda") ||
    typeText.includes("spu")
  ) {
    return "subtitle";
  }

  return "";
}

function getTrackId(name, stream) {
  const categoryId = Number((String(name).match(/\d+/) || [])[0]);
  if (Number.isInteger(categoryId)) {
    return categoryId;
  }

  return parseFirstInteger(
    readInsensitive(stream, "ID") ||
    readInsensitive(stream, "Id") ||
    readInsensitive(stream, "Track ID") ||
    readInsensitive(stream, "ID da faixa") ||
    readInsensitive(stream, "Id da faixa")
  );
}

function readInsensitive(object, key) {
  if (!object) {
    return "";
  }

  const normalizedKey = normalizeText(key);
  const foundKey = Object.keys(object).find((item) => normalizeText(item) === normalizedKey);
  return foundKey ? object[foundKey] : "";
}

function formatTrack(kind, id, index, name, stream) {
  const language = normalizeLanguage(readFirst(stream, ["Language", "Idioma"]));
  const title = readFirst(stream, ["Title", "Titulo", "Título", "Name", "Nome"]);
  const description = readFirst(stream, ["Description", "Descricao", "Descrição"]);
  const codec = readFirst(stream, ["Codec"]);
  const channels = readFirst(stream, ["Channels", "Canais"]);
  const bitrate = readFirst(stream, ["Bitrate", "Taxa de bits"]);

  return {
    id,
    kind,
    title: uniqueValues([title, description, language]).join(" - ") || fallbackTrackTitle(kind, index),
    detail: uniqueValues([codec, channels, bitrate, cleanTrackSource(name)]).join(" - ")
  };
}

function readFirst(object, keys) {
  for (const key of keys) {
    const value = cleanValue(readInsensitive(object, key));
    if (value) {
      return value;
    }
  }

  return "";
}

function cleanValue(value) {
  if (value === undefined || value === null) {
    return "";
  }

  return String(value).replace(/\s+/g, " ").trim();
}

function normalizeLanguage(value) {
  const cleaned = cleanValue(value);
  return /^(und|unknown|desconhecido)$/i.test(cleaned) ? "" : cleaned;
}

function cleanTrackSource(name) {
  const cleaned = cleanValue(name);
  return /\d/.test(cleaned) ? cleaned : "";
}

function fallbackTrackTitle(kind, index) {
  return `${kind === "audio" ? "Áudio" : "Legenda"} ${index + 1}`;
}

function uniqueValues(values) {
  const seen = new Set();
  const result = [];

  for (const value of values.map(cleanValue).filter(Boolean)) {
    const key = normalizeText(value);
    if (!seen.has(key)) {
      seen.add(key);
      result.push(value);
    }
  }

  return result;
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

function parseFirstInteger(value) {
  const match = String(value || "").match(/-?\d+/);
  return match ? Number(match[0]) : NaN;
}

function normalizeText(value) {
  return String(value || "")
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase();
}

export { normalizeStatus };
