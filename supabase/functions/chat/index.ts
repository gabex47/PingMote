import "jsr:@supabase/functions-js/edge-runtime.d.ts";

import { SYSTEM_PROMPT } from "./prompt.ts";
import { sanitizeReply } from "./reply.ts";

const MAX_MESSAGE_BYTES = 2_000;
const PROVIDER_TIMEOUT_MS = 15_000;

type ProviderConfig = Readonly<{
  apiUrl: string;
  apiKey: string;
  model: string;
}>;

const jsonHeaders = {
  "cache-control": "no-store",
  "content-type": "application/json; charset=utf-8",
  "x-content-type-options": "nosniff",
} as const;

function jsonResponse(body: Record<string, string>, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: jsonHeaders,
  });
}

function readProviderConfig(): ProviderConfig | null {
  const apiUrl = Deno.env.get("LLM_API_URL")?.trim() ?? "";
  const apiKey = Deno.env.get("LLM_API_KEY")?.trim() ?? "";
  const model = Deno.env.get("LLM_MODEL")?.trim() ?? "";

  if (apiUrl.length === 0 || apiKey.length === 0 || model.length === 0) {
    return null;
  }

  try {
    if (new URL(apiUrl).protocol !== "https:") {
      return null;
    }
  } catch {
    return null;
  }

  return { apiUrl, apiKey, model };
}

function extractProviderReply(payload: unknown): string | null {
  if (payload === null || typeof payload !== "object") {
    return null;
  }

  const choices = Reflect.get(payload, "choices");
  if (!Array.isArray(choices) || choices.length === 0) {
    return null;
  }

  const firstChoice = choices[0];
  if (firstChoice === null || typeof firstChoice !== "object") {
    return null;
  }

  const message = Reflect.get(firstChoice, "message");
  if (message === null || typeof message !== "object") {
    return null;
  }

  const content = Reflect.get(message, "content");
  return typeof content === "string" ? content : null;
}

async function requestReply(config: ProviderConfig, message: string): Promise<string> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), PROVIDER_TIMEOUT_MS);

  try {
    const response = await fetch(config.apiUrl, {
      method: "POST",
      headers: {
        authorization: `Bearer ${config.apiKey}`,
        "content-type": "application/json",
      },
      body: JSON.stringify({
        model: config.model,
        messages: [
          { role: "system", content: SYSTEM_PROMPT },
          { role: "user", content: message },
        ],
        max_tokens: 60,
        temperature: 0.9,
        stream: false,
      }),
      signal: controller.signal,
    });

    if (!response.ok) {
      console.error("language model request failed", { status: response.status });
      throw new Error("provider request failed");
    }

    const reply = extractProviderReply(await response.json());
    if (reply === null) {
      throw new Error("provider response was invalid");
    }

    return sanitizeReply(reply);
  } finally {
    clearTimeout(timeout);
  }
}

Deno.serve(async (request: Request): Promise<Response> => {
  if (request.method !== "POST") {
    return jsonResponse({ error: "method not allowed" }, 405);
  }

  const contentType = request.headers.get("content-type")?.toLowerCase() ?? "";
  if (!contentType.startsWith("application/json")) {
    return jsonResponse({ error: "content type must be application/json" }, 415);
  }

  let payload: unknown;
  try {
    payload = await request.json();
  } catch {
    return jsonResponse({ error: "invalid json" }, 400);
  }

  if (payload === null || typeof payload !== "object") {
    return jsonResponse({ error: "invalid request" }, 400);
  }

  const rawMessage = Reflect.get(payload, "message");
  if (typeof rawMessage !== "string") {
    return jsonResponse({ error: "message must be a string" }, 400);
  }

  const message = rawMessage.trim();
  const messageBytes = new TextEncoder().encode(message).byteLength;
  if (message.length === 0 || messageBytes > MAX_MESSAGE_BYTES) {
    return jsonResponse({ error: "message must contain 1 to 2000 bytes" }, 400);
  }

  const config = readProviderConfig();
  if (config === null) {
    console.error("language model secrets are missing or invalid");
    return jsonResponse({ error: "chat service is not configured" }, 503);
  }

  try {
    return jsonResponse({ reply: await requestReply(config, message) });
  } catch (error) {
    const reason = error instanceof DOMException && error.name === "AbortError"
      ? "timeout"
      : "provider failure";
    console.error("chat request failed", { reason });
    return jsonResponse({ error: "chat service unavailable" }, 502);
  }
});
