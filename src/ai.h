#pragma once
#include <stdint.h>

/* ai - the soupOS serial bridge to a host-side LLM.
 *
 * The OS can't run an LLM in-kernel, so `ai` talks over COM2 to a small host
 * daemon (tools/ai_bridge.py) that forwards the prompt to a real model (e.g.
 * Ollama) and streams the reply back. Protocol is deliberately trivial:
 *
 *   kernel -> host :  <prompt text> '\n'
 *   host   -> kernel: <reply bytes> AI_EOT
 *
 * The reply may contain newlines; AI_EOT (0x04) marks the end. The channel is
 * polled; ai_getc yields to the scheduler while it waits so background tasks
 * keep running and a missing daemon just times out.
 */
#define AI_EOT 0x04

void ai_init(void);                    /* probe + set up COM2            */
int  ai_available(void);               /* 1 if the COM2 channel exists   */
void ai_send(const char *prompt);      /* drain RX, send prompt + '\n'   */
int  ai_getc(uint32_t timeout_ticks);  /* next reply byte, or -1 timeout */
