/*
 * Entry point cho firmware release.
 *
 * Logic van dung chung voi main.cpp de debug va release khong bi lech thuat toan.
 * DEBUG_MODE=0 loai menu test, Serial console, local web UI va cac lenh hieu chuan
 * khoi binary; luong van hanh WiFi/WebSocket/OTA va dieu phoi route van duoc giu lai.
 */
#ifndef DEBUG_MODE
#define DEBUG_MODE 0
#endif

#include "main.cpp"
