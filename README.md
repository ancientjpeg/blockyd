# blockyd

`blockyd` installs Blocky and a small `libmicrohttpd` WebUI as systemd services.

The WebUI is intentionally minimal. It shows Blocky's blocking status and has buttons for enable, disable, list refresh, and cache flush.

## Requirements

- Linux with systemd
- `curl`, `tar`, `make`, `cc`, `pkg-config`
- distro `libmicrohttpd` development package

## Install

```sh
sudo ./setup.sh
```

Repeatable install example:

```sh
sudo ./setup.sh --yes --upstream 1.1.1.1,9.9.9.9
```

The installer preserves an existing `/etc/blockyd/blocky.yml`.
Re-running the installer rebuilds and reinstalls the WebUI, refreshes the pinned Blocky binary, rewrites the systemd units, and restarts both services. If services are already running, DNS and the WebUI briefly stop while binaries are replaced.

## Services

- `blockyd.service`: Blocky DNS on port `53`, API on `127.0.0.1:4000`
- `blockyd-httpd.service`: WebUI on `0.0.0.0:80`

Useful commands:

```sh
sudo systemctl status blockyd.service
sudo systemctl status blockyd-httpd.service
sudo journalctl -u blockyd.service -u blockyd-httpd.service
```

## Uninstall

```sh
sudo systemctl disable --now blockyd-httpd.service blockyd.service
sudo rm -f /etc/systemd/system/blockyd-httpd.service /etc/systemd/system/blockyd.service
sudo systemctl daemon-reload
sudo rm -f /usr/local/bin/blockyd-httpd /usr/local/bin/blocky
sudo rm -rf /etc/blockyd
sudo userdel blockyd
sudo groupdel blockyd 2>/dev/null || true
```
