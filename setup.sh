#!/usr/bin/env bash
set -euo pipefail

BLOCKY_VERSION="v0.30.0"
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="/etc/blockyd"
CONFIG_FILE="$CONFIG_DIR/blocky.yml"
USER_NAME="blockyd"
YES=0
UPSTREAMS=""

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
info() { printf '%s\n' "$*"; }

usage() {
    printf 'usage: %s [--yes] [--upstream server[,server...]]\n' "$0"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --yes|-y) YES=1 ;;
        --upstream) shift; [ "$#" -gt 0 ] || die "--upstream needs a value"; UPSTREAMS="$1" ;;
        --help|-h) usage; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
    shift
done

[ "$(id -u)" -eq 0 ] || die "run setup.sh as root"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

need() { command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"; }
for cmd in curl tar make cc pkg-config systemctl install useradd groupadd getent; do
    need "$cmd"
done
pkg-config --exists libmicrohttpd || die "missing libmicrohttpd development package"

if [ "$YES" -eq 0 ]; then
    info "This installs blockyd to $INSTALL_DIR, writes systemd units, and creates $CONFIG_DIR if needed."
    printf 'Continue? [y/N] '
    read -r ans
    case "$ans" in y|Y|yes|YES) ;; *) die "aborted" ;; esac
fi

port53_in_use() {
    command -v ss >/dev/null 2>&1 || return 1
    ss -H -lntu 2>/dev/null | grep -Eq '(:|\])53[[:space:]]'
}

if port53_in_use; then
    info "Port 53 appears to be in use."
    if [ "$YES" -eq 0 ]; then
        printf 'Continue anyway? [y/N] '
        read -r ans
        case "$ans" in y|Y|yes|YES) ;; *) die "aborted" ;; esac
    fi
fi

if [ -z "$UPSTREAMS" ]; then
    if [ "$YES" -eq 0 ]; then
        printf 'Upstream DNS servers [1.1.1.1]: '
        read -r UPSTREAMS
    fi
    UPSTREAMS="${UPSTREAMS:-1.1.1.1}"
fi

case "$(uname -m)" in
    x86_64|amd64) BLOCKY_ARCH="x86_64" ;;
    aarch64|arm64) BLOCKY_ARCH="arm64" ;;
    armv6*) BLOCKY_ARCH="armv6" ;;
    armv7*|armhf) BLOCKY_ARCH="armv7" ;;
    *) die "unsupported architecture: $(uname -m)" ;;
esac

[ "$(uname -s)" = "Linux" ] || die "systemd install is Linux-only"

if ! getent group "$USER_NAME" >/dev/null 2>&1; then
    groupadd --system "$USER_NAME"
fi

if ! id -u "$USER_NAME" >/dev/null 2>&1; then
    NOLOGIN="/usr/sbin/nologin"
    [ -x /sbin/nologin ] && NOLOGIN="/sbin/nologin"
    useradd --system --gid "$USER_NAME" --home-dir /nonexistent --shell "$NOLOGIN" "$USER_NAME"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

ASSET="blocky_${BLOCKY_VERSION}_Linux_${BLOCKY_ARCH}.tar.gz"
BASE_URL="https://github.com/0xERR0R/blocky/releases/download/${BLOCKY_VERSION}"
curl -fsSL -o "$TMP/$ASSET" "$BASE_URL/$ASSET"
curl -fsSL -o "$TMP/blocky_checksums.txt" "$BASE_URL/blocky_checksums.txt"

EXPECTED="$(awk -v f="$ASSET" '$2 == f {print $1}' "$TMP/blocky_checksums.txt")"
if [ -n "$EXPECTED" ]; then
    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL="$(sha256sum "$TMP/$ASSET" | awk '{print $1}')"
    elif command -v shasum >/dev/null 2>&1; then
        ACTUAL="$(shasum -a 256 "$TMP/$ASSET" | awk '{print $1}')"
    else
        ACTUAL=""
    fi
    [ -z "${ACTUAL:-}" ] || [ "$ACTUAL" = "$EXPECTED" ] || die "checksum mismatch for $ASSET"
fi

mkdir -p "$TMP/blocky"
tar -xzf "$TMP/$ASSET" -C "$TMP/blocky"
[ -x "$TMP/blocky/blocky" ] || die "blocky binary not found in archive"

make -C "$SCRIPT_DIR" clean all
install -d -m 0755 "$INSTALL_DIR"
install -m 0755 "$TMP/blocky/blocky" "$INSTALL_DIR/blocky"
install -m 0755 "$SCRIPT_DIR/blockyd-httpd" "$INSTALL_DIR/blockyd-httpd"

install -d -o "$USER_NAME" -g "$USER_NAME" -m 0755 "$CONFIG_DIR"
if [ ! -e "$CONFIG_FILE" ]; then
    {
        printf 'upstreams:\n  groups:\n    default:\n'
        IFS=',' read -r -a SERVERS <<< "$UPSTREAMS"
        for server in "${SERVERS[@]}"; do
            server="${server#"${server%%[![:space:]]*}"}"
            server="${server%"${server##*[![:space:]]}"}"
            [ -n "$server" ] && printf '      - %s\n' "$server"
        done
        printf '\nblocking:\n  denylists:\n    ads:\n      - https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts\n  clientGroupsBlock:\n    default:\n      - ads\n\nports:\n  dns: 53\n  http: 127.0.0.1:4000\n'
    } > "$CONFIG_FILE"
    chown "$USER_NAME:$USER_NAME" "$CONFIG_FILE"
    chmod 0644 "$CONFIG_FILE"
else
    info "Preserving existing $CONFIG_FILE"
fi

cat > /etc/systemd/system/blockyd.service <<EOF
[Unit]
Description=blockyd Blocky DNS service
After=network-online.target
Wants=network-online.target

[Service]
User=$USER_NAME
Group=$USER_NAME
ExecStart=$INSTALL_DIR/blocky --config $CONFIG_FILE
Restart=on-failure
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
ProtectHome=true
ProtectSystem=full
ReadWritePaths=$CONFIG_DIR

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/blockyd-httpd.service <<EOF
[Unit]
Description=blockyd WebUI
After=network-online.target blockyd.service
Wants=network-online.target blockyd.service

[Service]
User=$USER_NAME
Group=$USER_NAME
ExecStart=$INSTALL_DIR/blockyd-httpd
Restart=on-failure
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
ProtectHome=true
ProtectSystem=full

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable blockyd.service blockyd-httpd.service
systemctl restart blockyd.service blockyd-httpd.service

info "blockyd installed. WebUI listens on port 80."
