import { createServer } from "node:http";
import { createReadStream, existsSync } from "node:fs";
import { extname, join, normalize, resolve } from "node:path";
import { networkInterfaces } from "node:os";
import { loadDotEnv } from "./env.js";
import { getVlcStatus, sendControl } from "./vlcClient.js";

loadDotEnv();

const PORT = Number(process.env.PORT || 3000);
const HOST = process.env.HOST || "0.0.0.0";
const PUBLIC_DIR = resolve("public");
const MAX_BODY_BYTES = 1024 * 1024;

const MIME_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".ico": "image/x-icon"
};

const server = createServer(async (request, response) => {
  try {
    const url = new URL(request.url, `http://${request.headers.host || "localhost"}`);

    if (url.pathname === "/api/status" && request.method === "GET") {
      return sendJson(response, 200, { ok: true, status: await getVlcStatus() });
    }

    if (url.pathname === "/api/control" && request.method === "POST") {
      const body = await readJsonBody(request);
      return sendJson(response, 200, {
        ok: true,
        status: await sendControl(body.action, body.value)
      });
    }

    if (url.pathname.startsWith("/api/")) {
      return sendJson(response, 404, { ok: false, error: "Rota de API nao encontrada." });
    }

    return serveStatic(url.pathname, response);
  } catch (error) {
    const status = error.code === "BAD_REQUEST" ? 400 : 502;
    return sendJson(response, status, { ok: false, error: error.message || "Erro interno." });
  }
});

server.listen(PORT, HOST, () => {
  const localIps = getLocalIps();
  console.log(`VLC Control ouvindo em http://localhost:${PORT}`);
  for (const ip of localIps) {
    console.log(`Rede local: http://${ip}:${PORT}`);
  }
  console.log(`Destino VLC: ${process.env.VLC_PROTOCOL || "http"}://${process.env.VLC_HOST || "127.0.0.1"}:${process.env.VLC_PORT || "8080"}`);
});

function serveStatic(pathname, response) {
  const requestedPath = pathname === "/" ? "/index.html" : pathname;
  const normalizedPath = normalize(decodeURIComponent(requestedPath))
    .replace(/^(\.\.[/\\])+/, "")
    .replace(/^[/\\]+/, "");
  const filePath = resolve(join(PUBLIC_DIR, normalizedPath));

  if (!filePath.startsWith(PUBLIC_DIR) || !existsSync(filePath)) {
    response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Arquivo nao encontrado.");
    return;
  }

  response.writeHead(200, {
    "Content-Type": MIME_TYPES[extname(filePath)] || "application/octet-stream",
    "Cache-Control": "no-store"
  });

  createReadStream(filePath).pipe(response);
}

async function readJsonBody(request) {
  let size = 0;
  const chunks = [];

  for await (const chunk of request) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) {
      const error = new Error("Corpo da requisicao muito grande.");
      error.code = "BAD_REQUEST";
      throw error;
    }

    chunks.push(chunk);
  }

  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}");
  } catch {
    const error = new Error("JSON invalido.");
    error.code = "BAD_REQUEST";
    throw error;
  }
}

function sendJson(response, status, payload) {
  response.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store"
  });
  response.end(JSON.stringify(payload));
}

function getLocalIps() {
  return Object.values(networkInterfaces())
    .flat()
    .filter((entry) => entry && entry.family === "IPv4" && !entry.internal)
    .map((entry) => entry.address);
}
