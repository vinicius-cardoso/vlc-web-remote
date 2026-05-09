import assert from "node:assert/strict";
import test from "node:test";
import { normalizeStatus } from "../src/vlcClient.js";

test("extracts localized VLC audio and subtitle tracks with stream ids", () => {
  const status = normalizeStatus({
    state: "playing",
    volume: 128,
    information: {
      category: {
        meta: {
          title: "Filme"
        },
        "Fluxo 0": {
          Tipo: "Video",
          Codec: "H264"
        },
        "Fluxo 1": {
          Tipo: "Áudio",
          Idioma: "Português",
          Descrição: "5.1 principal",
          Codec: "A52"
        },
        "Fluxo 2": {
          Tipo: "Áudio",
          Idioma: "English",
          Descrição: "Original",
          Codec: "DTS"
        },
        "Fluxo 3": {
          Tipo: "Legenda",
          Idioma: "Português",
          Descrição: "Forçada",
          Codec: "SubRip"
        },
        "Fluxo 4": {
          Tipo: "Subtítulo",
          Idioma: "English",
          Descrição: "SDH",
          Codec: "SubRip"
        }
      }
    }
  });

  assert.deepEqual(
    status.tracks.audio.map((track) => [track.id, track.title, track.detail]),
    [
      [1, "5.1 principal - Português", "A52 - Fluxo 1"],
      [2, "Original - English", "DTS - Fluxo 2"]
    ]
  );

  assert.deepEqual(
    status.tracks.subtitles.map((track) => [track.id, track.title, track.detail]),
    [
      [3, "Forçada - Português", "SubRip - Fluxo 3"],
      [4, "SDH - English", "SubRip - Fluxo 4"]
    ]
  );
});

test("does not create fake tracks when VLC omits stream data", () => {
  const status = normalizeStatus({
    state: "playing",
    information: {
      category: {
        meta: {
          filename: "video.mkv"
        }
      }
    }
  });

  assert.deepEqual(status.tracks.audio, []);
  assert.deepEqual(status.tracks.subtitles, []);
});
