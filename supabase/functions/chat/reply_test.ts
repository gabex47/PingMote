import { assertEquals } from "jsr:@std/assert@1/equals";

import { sanitizeReply } from "./reply.ts";

Deno.test("normalizes replies to lowercase plain text", () => {
  assertEquals(sanitizeReply("**HELLO** [boss](https://example.com)"), "hello boss");
});

Deno.test("removes emoji and URLs", () => {
  assertEquals(sanitizeReply("yo 🫡 https://example.com ok"), "yo ok");
});

Deno.test("enforces the hard word limit", () => {
  const longReply = Array.from({ length: 25 }, (_, index) => `word${index}`).join(" ");
  assertEquals(sanitizeReply(longReply).split(" ").length, 20);
});

Deno.test("uses a safe fallback for empty output", () => {
  assertEquals(sanitizeReply("``` 🫡```"), "brain lag. try again.");
});
