import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

export function loadDotEnv(filePath = resolve(".env")) {
  if (!existsSync(filePath)) {
    return;
  }

  const lines = readFileSync(filePath, "utf8").split(/\r?\n/);

  for (const line of lines) {
    const trimmed = line.trim();

    if (!trimmed || trimmed.startsWith("#") || !trimmed.includes("=")) {
      continue;
    }

    const [key, ...valueParts] = trimmed.split("=");
    const name = key.trim();
    const value = cleanValue(valueParts.join("=").trim());

    if (name && process.env[name] === undefined) {
      process.env[name] = value;
    }
  }
}

function cleanValue(value) {
  if (
    (value.startsWith('"') && value.endsWith('"')) ||
    (value.startsWith("'") && value.endsWith("'"))
  ) {
    return value.slice(1, -1);
  }

  return value;
}
