# Optional Supabase backend operations

The Phase 2 desktop app calls the user's Groq account directly and does not require Supabase to chat. This backend remains deployed for future accounts, settings sync, explicit memories, and an optional hosted chat path.

## Project

- Project ref: `fclgpxemiseqozwwhktd`
- URL: `https://fclgpxemiseqozwwhktd.supabase.co`
- Edge endpoint: `https://fclgpxemiseqozwwhktd.supabase.co/functions/v1/chat`

## Data model

Supabase migrations create `profiles`, `settings`, and `memories`. A signup trigger creates the first two records atomically for each Auth user. A second trigger owns `settings.updated_at`. Every table has RLS enabled and separate select, insert, update, and delete ownership policies.

Conversations are deliberately not stored. `memories` accepts only explicit retained facts, constrains them to 1,000 characters, and uses an importance value from 1 through 5.

## Language-model secrets

The `chat` function accepts any HTTPS endpoint implementing the common chat-completions response shape. Configure secrets in Supabase, never in the desktop binary:

```sh
supabase secrets set \
  LLM_API_URL="https://provider.example/v1/chat/completions" \
  LLM_API_KEY="your-provider-secret" \
  LLM_MODEL="your-chat-model" \
  --project-ref fclgpxemiseqozwwhktd
```

The repository contains names only in `supabase/.env.example`; actual `.env` files are ignored. Supabase-managed variables such as `SUPABASE_URL` should not be overridden.

## Deploy and verify

```sh
supabase db push --project-ref fclgpxemiseqozwwhktd
supabase functions deploy chat --project-ref fclgpxemiseqozwwhktd
```

JWT verification is enabled. Invoke with both the project publishable key and a signed-in user's access token:

```sh
curl --request POST \
  'https://fclgpxemiseqozwwhktd.supabase.co/functions/v1/chat' \
  --header 'apikey: sb_publishable_...' \
  --header 'Authorization: Bearer user-jwt' \
  --header 'Content-Type: application/json' \
  --data '{"message":"hi"}'
```

A successful response has exactly one public field:

```json
{"reply":"yo"}
```

The gateway rejects missing or invalid user JWTs. The function validates content type and a 2,000-byte input limit, applies a 15-second provider timeout, never logs messages or secrets, disables response caching, strips markdown/emoji/URLs, lowercases output, and enforces a 20-word hard limit.
