#ifndef PIN_LIST_H
#define PIN_LIST_H

// ── Servo GPIO pins ──────────────────────────────────────────────
#define SERVO_1_PIN  4   // First servo  (e.g. left side)
#define SERVO_2_PIN  5   // Second servo (right side, facing first)

// ── Optional Wired UART Fallback (if ESP-NOW is not used) ────────
#define OPTIONAL_UART_RX  16
#define OPTIONAL_UART_TX  17

#endif // PIN_LIST_H