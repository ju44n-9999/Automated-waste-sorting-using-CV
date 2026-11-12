#pragma once

// Copy this file to llm_secrets.h (ignored by git) and fill in your keys.
// An empty key disables that provider.
//   Groq:   https://console.groq.com/keys
//   Gemini: https://aistudio.google.com/apikey
#define GROQ_API_KEY ""
#define GEMINI_API_KEY ""
// Optional extra Gemini keys, tried in order when the previous one is rate limited.
#define GEMINI_API_KEY_2 ""
#define GEMINI_API_KEY_3 ""
