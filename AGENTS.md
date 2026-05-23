# AGENTS.md

## Project Shape
- This is a minimal proof-of-concept: keep changes small, direct, and dependency-light unless asked otherwise.
- `src/blockyd-httpd.c` is the WebUI entrypoint; it uses distro `libmicrohttpd` and a tiny raw TCP HTTP client to call Blocky.
- `setup.sh` is the installer and the source of truth for service names, paths, generated config, and Blocky release selection.

## Commands
- Build the WebUI: `make clean all`
- Validate the installer syntax: `bash -n setup.sh`
- Repeatable install command: `sudo ./setup.sh --yes --upstream 1.1.1.1,9.9.9.9`
- A local build requires `pkg-config` to find `libmicrohttpd`; missing headers on macOS/dev hosts are expected unless that package is installed.

## Installer Constraints
- `setup.sh` is root-only, Linux/systemd-only, and pins Blocky to `v0.30.0`.
- Supported Blocky release arches are `x86_64`, `arm64/aarch64`, `armv6`, and `armv7`; first real target is BeagleBone Black/ARMv7.
- Install paths are `/usr/local/bin/blocky`, `/usr/local/bin/blockyd-httpd`, and `/etc/blockyd/blocky.yml`.
- Service names are `blockyd.service` and `blockyd-httpd.service`; both run as the dedicated `blockyd` user/group.
- Never overwrite an existing `/etc/blockyd/blocky.yml`; the installer intentionally only creates it when missing.

## Runtime Defaults
- WebUI listen address/port are compile-time constants: `0.0.0.0:80` via `LISTEN_PORT 80`.
- Blocky API target is compile-time: `127.0.0.1:4000`.
- Generated Blocky config exposes DNS on all interfaces port `53` and HTTP API only on `127.0.0.1:4000`.
- The WebUI must stay up when Blocky is down and show `Blocky unavailable`; do not make `blockyd-httpd.service` require `blockyd.service`.

## WebUI Scope
- Keep raw HTML and full-page refreshes; no JavaScript or auth in this skeleton.
- Current actions are global blocking status, enable, disable, list refresh, and cache flush.
