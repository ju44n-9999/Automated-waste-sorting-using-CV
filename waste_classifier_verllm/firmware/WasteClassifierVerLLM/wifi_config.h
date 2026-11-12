#pragma once
// Empty SSID starts the WasteCamera hotspot. Set these locally for home Wi-Fi.
static const char *WIFI_SSID = "";
static const char *WIFI_PASSWORD = "";
static const char *AP_SSID = "WasteCamera";
static const char *AP_PASSWORD = "wastecam123";
// Fixed radio channel for the hotspot and ESP-NOW. The camera only joins a
// router that is on this same channel, so it never drifts. Must match
// ESPNOW_WIFI_CHANNEL in devicesControl/include/config.h.
static const uint8_t WIFI_CHANNEL = 6;
