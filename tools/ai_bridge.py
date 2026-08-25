#!/usr/bin/env python3
"""ai_bridge.py — host side of the soupOS `ai` serial bridge.

soupOS talks to this daemon over COM2 (a QEMU chardev). Protocol:

    soupOS -> bridge :  <prompt text> '\\n'
    bridge -> soupOS :  <reply bytes> 0x04(EOT)

The bridge forwards each prompt to a real LLM and streams the reply back. By
default it asks a local Ollama (http://localhost:11434); set AI_FAKE=1 for a
deterministic offline reply (used by the automated round-trip test). Every
prompt is appended to AI_LOG so the kernel->host direction is verifiable.

Usage:
    python3 tools/ai_bridge.py [socket-path]      # default /tmp/soupos-ai.sock

Env:
    AI_FAKE=1            canned reply, no network
    AI_MODEL=<name>      Ollama model (default: llama3.2)
    AI_OLLAMA=<url>      Ollama base URL (default: http://localhost:11434)
    AI_LOG=<path>        prompt log (default: /tmp/ai_bridge.log)
"""
import os, sys, time, socket, json, urllib.request

EOT       = b"\x04"
SOCK_PATH = sys.argv[1] if len(sys.argv) > 1 else "/tmp/soupos-ai.sock"
LOG_PATH  = os.environ.get("AI_LOG", "/tmp/ai_bridge.log")
MODEL     = os.environ.get("AI_MODEL", "llama3.2")
OLLAMA    = os.environ.get("AI_OLLAMA", "http://localhost:11434")
FAKE      = os.environ.get("AI_FAKE") == "1"


def log(msg):
    with open(LOG_PATH, "a") as f:
        f.write(msg + "\n")


def ask_ollama(prompt):
    body = json.dumps({"model": MODEL, "prompt": prompt, "stream": False}).encode()
    req = urllib.request.Request(OLLAMA + "/api/generate", data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())["response"].strip()


def respond(prompt):
    if FAKE:
        return "soupOS oracle says: " + prompt.upper()
    try:
        return ask_ollama(prompt)
    except Exception as e:                       # network down / no model
        return ("The broth is cold (no LLM reachable: %s). "
                "Try `make run-ai` with Ollama up, or AI_FAKE=1." % e)


def connect():
    """Connect to the QEMU-created unix socket, retrying until it appears."""
    for _ in range(200):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCK_PATH)
            return s
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.1)
    raise SystemExit(f"ai_bridge: could not connect to {SOCK_PATH}")


def main():
    print(f"ai_bridge: connecting to {SOCK_PATH} "
          f"({'FAKE' if FAKE else MODEL + ' @ ' + OLLAMA})", flush=True)
    s = connect()
    print("ai_bridge: connected — waiting for prompts", flush=True)
    buf = bytearray()
    while True:
        try:
            data = s.recv(4096)
        except OSError:
            break
        if not data:                            # guest/QEMU went away
            time.sleep(0.2)
            try:
                s = connect()
                continue
            except SystemExit:
                break
        for b in data:
            if b == 0x0A:                        # '\n' ends a prompt
                prompt = buf.decode("utf-8", "replace").strip()
                buf.clear()
                if not prompt:
                    continue
                log("PROMPT: " + prompt)
                reply = respond(prompt)
                print(f"ai_bridge: <- {prompt!r}  -> {reply[:60]!r}", flush=True)
                s.sendall(reply.encode("utf-8", "replace") + EOT)
            else:
                buf.append(b)


if __name__ == "__main__":
    main()
