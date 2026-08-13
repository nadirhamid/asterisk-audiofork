# AudioFork – Asterisk Audio Streaming over WebSocket

📖 **Read this document in Persian:** [README_fa.md](README_fa.md)

---

## 1. Project Title & Introduction

**English**

**AudioFork** is an Asterisk module that forks raw audio streams from an Asterisk channel to a remote WebSocket server in real time. This enables a wide range of live processing use cases such as speech‑to‑text transcription, call recording, sentiment/voice analytics, acoustic detection, and any downstream application that consumes live audio.

The module is built on top of Asterisk's `ast_audiohook` API, which lets it intercept a channel's audio without disturbing the call. The **original** `app_audiofork` (by Nadir Hamid) introduced the basic ability to stream SLIN 8 kHz audio to a WebSocket. This **updated version** adds configurable audio formats (codec and sample rate), robust reconnection with exponential backoff, custom HTTP headers and Bearer‑token authentication, WebSocket subprotocol negotiation, JSON metadata transmission, header sanitisation, and precise interruption handling — based on real‑world production needs.

---

## 2. Credits & License

**English**

_Original author:_ **Nadir Hamid** – original `app_audiofork` project. (Repository reference: the original source is widely known in the Asterisk community; see the upstream `app_audiofork` by Nadir Hamid.)

_Maintainer & contributor:_ **Hossein Mohhmadian (Hosseinhunta)** – [https://github.com/hosseinhunta](https://github.com/hosseinhunta). This version has been maintained, improved, and had critical bugs fixed based on real‑world production needs. The code is now more robust and secure.

_License:_ GNU General Public License v2.0. See the `LICENSE` file in this repository. This is a derivative work; both the original and this enhanced version are distributed under GPLv2.

---

## 3. Key Features

**English**

| Feature                           | Option   | Description                                                                                                                                                                               |
| :-------------------------------- | :------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Multiple audio formats            | `c`, `L` | SLIN at 8/16/32/48 kHz, plus codecs G722, PCMU, PCMA. Asterisk transcodes automatically.                                                                                                  |
| Custom HTTP headers & Bearer auth | `H`, `A` | Sent during the WebSocket handshake. Uses `ast_websocket_client_create_with_headers` when available; otherwise a clear warning is logged and headers fall back to subprotocol/URL params. |
| WebSocket subprotocol             | `s`      | E.g. `audio/raw`. Negotiated during the handshake (defaults to `echo`).                                                                                                                   |
| JSON metadata                     | `M`      | A TEXT frame (channel, caller ID, dialed number, start time, sample rate, codec) is sent before the audio stream.                                                                         |
| Exponential backoff               | –        | Delay doubles each attempt, capped at 30 s, up to `r` attempts.                                                                                                                           |
| Fast interruption                 | –        | `StopAudioFork` is honoured within ~100 ms (nanosleep‑based).                                                                                                                             |
| Header sanitisation               | –        | Control characters stripped; malformed pairs skipped to prevent injection.                                                                                                                |
| Metadata retry                    | –        | Up to 3 attempts with 200 ms delay; failure is logged but audio continues.                                                                                                                |

---

## 4. Installation & Build

**English**

_Prerequisites_

- Asterisk **13+** (tested on 18.x / 20.x).
- Runtime modules loaded in Asterisk: `func_periodic_hook` (optional, for periodic beep), `codec_g722`, `codec_ulaw`, `codec_alaw`, and `res_websocket`.
- Development headers from your Asterisk source tree.

_Build & install_

```bash
# Point ASTTOPDIR at your Asterisk source tree
make ASTTOPDIR=/usr/src/asterisk
sudo make install ASTTOPDIR=/usr/src/asterisk
```

_Load the module_

```bash
# From the Asterisk CLI:
module load app_audiofork.so

# To auto-load at startup, add to /etc/asterisk/modules.conf:
[modules]
load => app_audiofork.so
```

---

## 5. Usage Guide

**English**

_Dialplan example_

```text
exten => 100,1,Answer()
exten => 100,n,AudioFork(ws://127.0.0.1:8080/in,c(SLIN)L(8000)s(audio/raw)M,A(my-jwt)H(X-Api-Key:abc123))
```

_Option reference_

| Option       | Meaning                                                                        |
| :----------- | :----------------------------------------------------------------------------- |
| `c(codec)`   | Codec: `SLIN`, `G722`, `PCMU`, `PCMA`.                                         |
| `L(rate)`    | SLIN sample rate: `8000`, `16000`, `32000`, `48000`. Ignored for other codecs. |
| `s(proto)`   | WebSocket subprotocol (e.g. `audio/raw`).                                      |
| `M`          | Enable JSON metadata frame before audio.                                       |
| `A(token)`   | Bearer token; adds `Authorization: Bearer <token>` header.                     |
| `H(k:v,k:v)` | Custom headers as comma‑separated `Key:Value` pairs.                           |
| `R(sec)`     | Initial reconnection delay in seconds (also the backoff base).                 |
| `r(n)`       | Maximum number of reconnection attempts.                                       |
| `T(cert)`    | TLS certificate path for secure (`wss://`) connections.                        |
| `D(dir)`     | Direction: `in`, `out`, or `both` (default).                                   |

_AMI actions_

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

_CLI commands_

```bash
asterisk -rx "audiofork start PJSIP/1001-00000001 ws://127.0.0.1:8080/in,c(SLIN)L(8000)M"
asterisk -rx "audiofork stop PJSIP/1001-00000001"
asterisk -rx "audiofork list PJSIP/1001-00000001"
```

---

## 6. Architecture Overview

**English**

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

- `struct audiofork` – per‑session state owned by the fork thread: the audiohook, the resolved `ast_format`, codec/rate/subprotocol/headers, metadata flag, and flags.
- `struct audiofork_ds` – a datastore attached to the channel holding the WebSocket URL, format, and synchronisation locks. **Lifetime is single‑owner**: the fork thread frees it after `ast_cond_wait()` and NULLs `audiofork->audiofork_ds`; `audiofork_free()` only frees it when non‑NULL. Preserving this invariant avoids a double‑free / use‑after‑free.

---

## 7. Fixes & Improvements Over the Original

**English**

1. **Fixed critical double‑free of `audiofork_ds`.** Ownership moved to the fork thread, which frees the structure after the destruction condition is met and NULLs the pointer so `audiofork_free()` cannot free it twice.
2. **Actually transmit custom headers and Bearer token.** The module resolves `ast_websocket_client_create_with_headers` at runtime via `dlsym()`. When present, `-H`/`-A` headers are sent during the handshake; otherwise a clear `LOG_WARNING` explains headers are not transmitted (use subprotocol or URL `?token=...`).
3. **Added input validation for headers.** `audiofork_normalize_headers()` strips control characters and skips malformed pairs (missing `:`), preventing header‑injection attacks.
4. **Improved `audiofork_sleep_interruptible` precision.** Replaced the `sleep(1)` loop with `nanosleep()` at 100 ms granularity, so `StopAudioFork` is honoured within ~100 ms.
5. **Added retry logic for metadata transmission.** The JSON metadata send retries up to 3 times with a 200 ms delay; on failure it logs an error but continues the audio stream.

---

## 8. Troubleshooting

**English**

| Issue                  | Solution                                                                                                                                                                                                                   |
| :--------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Headers not sent**   | On stock Asterisk, `ast_websocket_client_create_with_headers` is unavailable. Pass credentials via the subprotocol or a URL query string, e.g. `ws://host/stream?token=...`. A `LOG_WARNING` is printed when this happens. |
| **Reconnection fails** | Check your `R` (initial delay) and `r` (attempts) values and verify network reachability/firewall to the WebSocket server.                                                                                                 |
| **Memory leaks**       | Always stop a fork cleanly: call `StopAudioFork` (dialplan/AMI/CLI) or let the channel hang up; this triggers the datastore cleanup.                                                                                       |
| **Module not loading** | Ensure prerequisite modules are loaded: `res_websocket`, `codec_g722`, `codec_ulaw`, `codec_alaw` (and `func_periodic_hook` if using the beep). Confirm it was built against the correct Asterisk version.                 |

---

## 9. Contributing

**English**

Contributions, bug reports, and feature requests are welcome. Please open an issue or pull request on the maintained fork: [https://github.com/hosseinhunta](https://github.com/hosseinhunta). When reporting problems, include the Asterisk version, the exact `AudioFork(...)` options used, and any `[AudioFork]` log lines.

---

## 10. Changelog

**English**

| Version              | Author                            | Notes                                                                                                                                                                                                                                                                                                                                                                   |
| :------------------- | :-------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Original             | Nadir Hamid                       | Basic WebSocket audio forking (SLIN 8 kHz).                                                                                                                                                                                                                                                                                                                             |
| Enhanced (this fork) | Hossein Mohhmadian (Hosseinhunta) | Configurable codec/rate (`c`,`L`); subprotocol (`s`); custom headers & Bearer auth (`H`,`A`) via runtime `dlsym` of `ast_websocket_client_create_with_headers`; JSON metadata (`M`) with 3‑retry send; exponential‑backoff reconnection (`R`,`r`); `nanosleep`‑based 100 ms interruption; header input sanitisation; critical double‑free fix of `audiofork_ds`. GPLv2. |

---

_License: GNU General Public License v2.0 — see the `LICENSE` file._
