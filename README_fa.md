# AudioFork – Real‑time Asterisk Audio Streaming over WebSocket

[![License](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)
[![Asterisk](https://img.shields.io/badge/Asterisk-13%2B-green.svg)](https://www.asterisk.org/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](http://makeapullrequest.com)
[![GitHub stars](https://img.shields.io/github/stars/hosseinhunta/asterisk-audiofork.svg?style=social&label=Star&maxAge=2592000)](https://github.com/hosseinhunta/asterisk-audiofork)

📖 **Read this document in Persian:** [README_fa.md](README_fa.md)

---

## 🚀 Quick Start

Stream live audio from any Asterisk channel to a WebSocket server in just a few lines:

```text
exten => 100,1,Answer()
exten => 100,n,AudioFork(ws://your-server:8080/in,c(SLIN)L(8000)M)
```

That's it! Your audio stream is now available for real‑time processing (speech‑to‑text, analytics, recording, etc.).

---

## 1. Project Introduction

**AudioFork** is a high‑performance **Asterisk module** that captures raw audio from a live channel and forwards it **in real‑time** to a **WebSocket server**. This unlocks unlimited possibilities for **live voice processing**, including:

- 🎤 **Speech‑to‑Text** (STT) transcription
- 📞 **Call recording** and archiving
- 📊 **Sentiment & voice analytics**
- 🔊 **Acoustic event detection**
- 🤖 **AI‑powered voice assistants**

Built on Asterisk’s powerful `ast_audiohook` API, it intercepts audio **without disrupting** the ongoing call. The original `app_audiofork` (by **Nadir Hamid**) provided basic SLIN 8 kHz streaming. This **enhanced version** adds production‑ready features: **configurable codecs** (SLIN/G722/PCMU/PCMA), **sample rates** up to 48 kHz, **Bearer token authentication**, **custom HTTP headers**, **WebSocket subprotocol negotiation**, **JSON metadata**, **exponential‑backoff reconnection**, **header sanitisation**, and **sub‑100ms interruption** – all based on real‑world deployment needs.

---

## 2. Why AudioFork?

| Challenge               | How AudioFork Solves It                                                   |
| :---------------------- | :------------------------------------------------------------------------ |
| **Complex transcoding** | Automatically transcodes channel audio to your desired codec/rate.        |
| **Authentication**      | Supports Bearer tokens and custom headers (with runtime fallback).        |
| **Network instability** | Intelligent reconnection with exponential backoff keeps the stream alive. |
| **Real‑time metadata**  | Sends JSON metadata before the audio stream for context‑aware processing. |
| **Security**            | Built‑in header sanitisation prevents injection attacks.                  |
| **Performance**         | Low‑latency, efficient audio streaming with minimal CPU overhead.         |

---

## 3. Key Features

| Feature                          | Option   | Description                                                                                      |
| :------------------------------- | :------- | :----------------------------------------------------------------------------------------------- |
| **Multiple audio formats**       | `c`, `L` | SLIN at 8/16/32/48 kHz, G722, PCMU, PCMA. Auto‑transcoded.                                       |
| **Custom headers & Bearer auth** | `H`, `A` | Sent during WebSocket handshake. Uses `ast_websocket_client_create_with_headers` when available. |
| **WebSocket subprotocol**        | `s`      | E.g. `audio/raw`; defaults to `echo`.                                                            |
| **JSON metadata**                | `M`      | TEXT frame with channel, caller ID, dialed number, start time, codec, sample rate.               |
| **Exponential backoff**          | –        | Delay doubles each attempt, capped at 30 s, up to `r` attempts.                                  |
| **Fast interruption**            | –        | `StopAudioFork` honoured within ~100 ms (nanosleep‑based).                                       |
| **Header sanitisation**          | –        | Strips control chars; skips malformed pairs to prevent injection.                                |
| **Metadata retry**               | –        | Up to 3 attempts with 200 ms delay; audio continues on failure.                                  |

---

## 4. Use Cases

- **Real‑time Transcription** – Feed audio directly to STT engines (e.g., Google Cloud, Azure, Whisper).
- **Call Analytics** – Analyse customer sentiment or detect keywords during live calls.
- **Voice Biometrics** – Identify speakers or detect anomalies in real time.
- **Recording & Archiving** – Store audio streams in cloud storage or local servers.
- **AI Assistants** – Enable voice‑enabled bots that respond during calls.

---

## 5. Installation & Build

### Prerequisites

- Asterisk **13+** (tested on 18.x / 20.x)
- Loaded modules: `func_periodic_hook` (optional), `codec_g722`, `codec_ulaw`, `codec_alaw`, `res_websocket`
- Development headers from your Asterisk source tree

### Build & Install

```bash
make ASTTOPDIR=/usr/src/asterisk
sudo make install ASTTOPDIR=/usr/src/asterisk
```

### Load the Module

```bash
# From Asterisk CLI:
module load app_audiofork.so

# Auto‑load at startup – add to /etc/asterisk/modules.conf:
[modules]
load => app_audiofork.so
```

---

## 6. Usage Guide

### Dialplan Example

```text
exten => 100,1,Answer()
exten => 100,n,AudioFork(ws://127.0.0.1:8080/in,c(SLIN)L(8000)s(audio/raw)M,A(my-jwt)H(X-Api-Key:abc123))
```

### Option Reference

| Option       | Meaning                                                                        |
| :----------- | :----------------------------------------------------------------------------- |
| `c(codec)`   | Codec: `SLIN`, `G722`, `PCMU`, `PCMA`                                          |
| `L(rate)`    | SLIN sample rate: `8000`, `16000`, `32000`, `48000` (ignored for other codecs) |
| `s(proto)`   | WebSocket subprotocol (e.g., `audio/raw`)                                      |
| `M`          | Enable JSON metadata frame before audio                                        |
| `A(token)`   | Bearer token – adds `Authorization: Bearer <token>` header                     |
| `H(k:v,k:v)` | Custom headers as comma‑separated `Key:Value` pairs                            |
| `R(sec)`     | Initial reconnection delay in seconds (backoff base)                           |
| `r(n)`       | Max reconnection attempts                                                      |
| `T(cert)`    | TLS certificate path for secure (`wss://`) connections                         |
| `D(dir)`     | Direction: `in`, `out`, or `both` (default)                                    |

### AMI Actions

```text
Action: AudioFork
Channel: PJSIP/1001-00000001
WsServer: ws://127.0.0.1:8080/in
Options: c(SLIN)L(8000)s(audio/raw)M
ActionID: af1

Action: StopAudioFork
Channel: PJSIP/1001-00000001
ActionID: af2

Action: AudioForkMute
Channel: PJSIP/1001-00000001
Direction: both
State: 1
```

### CLI Commands

```bash
asterisk -rx "audiofork start PJSIP/1001-00000001 ws://127.0.0.1:8080/in,c(SLIN)L(8000)M"
asterisk -rx "audiofork stop PJSIP/1001-00000001"
asterisk -rx "audiofork list PJSIP/1001-00000001"
```

---

## 7. Architecture Overview

```
Asterisk channel
      │  ast_audiohook (spy) intercepts audio
      ▼
audiofork_thread
      │  ast_audiohook_read_frame(format)  → transcoded to requested codec/rate
      │
      ├─(once, if M)─▶ JSON metadata as TEXT frame
      │
      └─(loop)──────▶ audio frames as BINARY frames ──▶ WebSocket server
```

- `struct audiofork` – per‑session state owned by the fork thread: the audiohook, resolved `ast_format`, codec/rate/subprotocol/headers, metadata flag, and flags.
- `struct audiofork_ds` – datastore attached to the channel holding the WebSocket URL, format, and synchronisation locks. **Single‑owner lifetime**: the fork thread frees it after `ast_cond_wait()` and NULLs `audiofork->audiofork_ds`; `audiofork_free()` only frees it when non‑NULL. This prevents double‑free / use‑after‑free.

---

## 8. Fixes & Improvements Over the Original

1. **Fixed critical double‑free** of `audiofork_ds` – ownership moved to the fork thread.
2. **Actually transmit custom headers and Bearer token** – runtime `dlsym()` of `ast_websocket_client_create_with_headers` with clear fallback warning.
3. **Input validation for headers** – `audiofork_normalize_headers()` strips control chars and skips malformed pairs.
4. **Precise interruption** – replaced `sleep(1)` with `nanosleep()` (100 ms granularity).
5. **Metadata retry logic** – up to 3 attempts with 200 ms delay; audio continues on failure.

---

## 9. Troubleshooting

| Issue                  | Solution                                                                                                                                                                        |
| :--------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Headers not sent**   | On stock Asterisk, `ast_websocket_client_create_with_headers` is unavailable. Use subprotocol or URL query string (`?token=...`). A `LOG_WARNING` is printed.                   |
| **Reconnection fails** | Check `R` and `r` values, network reachability, and firewall to the WebSocket server.                                                                                           |
| **Memory leaks**       | Always stop the fork cleanly with `StopAudioFork` (dialplan/AMI/CLI) or let the channel hang up.                                                                                |
| **Module not loading** | Ensure prerequisites are loaded: `res_websocket`, `codec_g722`, `codec_ulaw`, `codec_alaw` (and `func_periodic_hook` for beep). Confirm build against correct Asterisk version. |

---

## 10. Support & Contributing

- **Report bugs** or **request features** via [GitHub Issues](https://github.com/hosseinhunta/asterisk-audiofork/issues).
- **Submit improvements** via Pull Requests.
- **Questions?** Open a discussion or reach out to the maintainer.

**Maintainer:** [Hossein Mohhmadian (Hosseinhunta)](https://github.com/hosseinhunta)

---

## 11. Changelog

| Version              | Author                            | Notes                                                                                                                                                                                                                                                                                      |
| :------------------- | :-------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Original             | Nadir Hamid                       | Basic WebSocket audio forking (SLIN 8 kHz).                                                                                                                                                                                                                                                |
| Enhanced (this fork) | Hossein Mohhmadian (Hosseinhunta) | Configurable codec/rate (`c`,`L`); subprotocol (`s`); custom headers & Bearer auth (`H`,`A`) via runtime `dlsym`; JSON metadata (`M`) with retry; exponential‑backoff reconnection (`R`,`r`); `nanosleep`‑based 100 ms interruption; header sanitisation; critical double‑free fix. GPLv2. |

---

**License:** GNU General Public License v2.0 – see the `LICENSE` file.
