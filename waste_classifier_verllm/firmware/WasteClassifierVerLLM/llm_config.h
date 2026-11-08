#pragma once

// Free-tier Groq key, stored in the firmware at the owner's request.
// Anyone who can read the chip can use this key.
#define GROQ_API_KEY ""
#define GROQ_MODEL "qwen/qwen3.8-27b"
#define GROQ_CHAT_URL "https://api.groq.com/openai/v1/chat/completions"
#define GROQ_PROBE_URL "https://api.groq.com/openai/v1/models/qwen/qwen3.8-27b"

#define LLM_CONNECT_TIMEOUT_MS 15000
#define LLM_HTTP_TIMEOUT_MS 60000
#define LLM_MAX_IMAGE_BYTES 120000
#define LLM_MAX_RESPONSE_BYTES 8192
#define LLM_CAPTURE_INTERVAL_MS 30000
#define LLM_RATE_LIMIT_BACKOFF_MS 60000
