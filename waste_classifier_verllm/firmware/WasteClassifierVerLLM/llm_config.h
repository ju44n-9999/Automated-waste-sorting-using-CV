#pragma once

// API keys live in llm_secrets.h (ignored by git); copy llm_secrets.example.h to create it.
#include "llm_secrets.h"
#ifndef GEMINI_API_KEY_2
#define GEMINI_API_KEY_2 ""
#endif
#ifndef GEMINI_API_KEY_3
#define GEMINI_API_KEY_3 ""
#endif

#define GROQ_MODEL "qwen/qwen3.8-27b"
#define GROQ_CHAT_URL "https://api.groq.com/openai/v1/chat/completions"
#define GROQ_PROBE_URL "https://api.groq.com/openai/v1/models/qwen/qwen3.8-27b"
#define GROQ_REQUEST_OPTIONS "\"reasoning_effort\":\"none\",\"max_completion_tokens\":100"

// Gemini fallback via its OpenAI-compatible endpoint. Key: https://aistudio.google.com/apikey
// An empty GEMINI_API_KEY disables Gemini. Check the model id against the current model list.
// Measured 2026-11-12 with a test image: gemini-3.5-flash ~4 s; gemini-3.8-flash was
// overloaded (503 / >30 s); gemini-3.5-flash-lite 2-12 s.
#define GEMINI_MODEL "gemini-3.5-flash"
#define GEMINI_CHAT_URL "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"
#define GEMINI_PROBE_URL "https://generativelanguage.googleapis.com/v1beta/openai/models/" GEMINI_MODEL
// Thinking tokens count toward the output limit, so give Gemini more room than Groq.
#define GEMINI_REQUEST_OPTIONS "\"reasoning_effort\":\"low\",\"max_tokens\":1024"

// Order: Groq -> Gemini (key 1, 2, 3) -> on-device model. A 429 backs off only that key. LLM_WATCHDOG_MS is the wall-clock budget
// for all LLM attempts together (connect + TLS + upload + reply). Groq gets at most
// LLM_PRIMARY_BUDGET_MS when Gemini is usable, so Gemini keeps the rest. A provider is
// not started with less than LLM_MIN_ATTEMPT_MS left.
#define LLM_WATCHDOG_MS 15000
#define LLM_PRIMARY_BUDGET_MS 8000
#define LLM_MIN_ATTEMPT_MS 3000
#define LLM_CONNECT_TIMEOUT_MS 15000
#define LLM_HTTP_TIMEOUT_MS 60000
#define LLM_MAX_IMAGE_BYTES 120000
#define LLM_MAX_RESPONSE_BYTES 8192
#define LLM_CAPTURE_INTERVAL_MS 30000
#define LLM_RATE_LIMIT_BACKOFF_MS 60000
