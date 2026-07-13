const HARD_WORD_LIMIT = 20;
const FALLBACK_REPLY = "brain lag. try again.";

export function sanitizeReply(input: string): string {
  const plainText = input
    .replace(/\[([^\]]+)]\([^)]+\)/g, "$1")
    .replace(/https?:\/\/\S+/gi, "")
    .replace(/\p{Extended_Pictographic}/gu, "")
    .replace(/[`*_~>#|]/g, "")
    .replace(/(^|\s)-\s+/g, "$1")
    .replace(/[\u0000-\u001f\u007f]/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .toLocaleLowerCase("en-US");

  if (plainText.length === 0) {
    return FALLBACK_REPLY;
  }

  return plainText.split(" ").slice(0, HARD_WORD_LIMIT).join(" ");
}
