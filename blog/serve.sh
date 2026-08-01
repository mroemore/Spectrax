#!/usr/bin/env bash
# serve.sh — Start a static HTTP server for the Spectrax blog.
#
# Usage: ./serve.sh
#
# Serves the blog/ directory on port 8080 and prints the Tailscale URL.
# Press Ctrl-C to stop.

set -e

# ── Resolve the directory this script lives in ──────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Cleanup handler ─────────────────────────────────────────────────────
cleanup() {
    echo ""
    echo "Shutting down server (PID ${SERVER_PID})..."
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    echo "Server stopped."
    exit 0
}

trap cleanup INT TERM

# ── Choose HTTP server ──────────────────────────────────────────────────
if command -v python3 &>/dev/null; then
    echo "Using python3 HTTP server."
    python3 -m http.server 8080 --directory "$SCRIPT_DIR" &
    SERVER_PID=$!
elif command -v busybox &>/dev/null; then
    echo "python3 not found — falling back to busybox httpd."
    busybox httpd -f -p 8080 -h "$SCRIPT_DIR" &
    SERVER_PID=$!
else
    echo "ERROR: Neither python3 nor busybox is available. Cannot start server." >&2
    exit 1
fi

# ── Print access URL ────────────────────────────────────────────────────
if command -v tailscale &>/dev/null; then
    TAILSCALE_IP="$(tailscale ip -4 2>/dev/null || echo "")"
    if [ -n "$TAILSCALE_IP" ]; then
        echo ""
        echo "=========================================="
        echo "  Blog server running on port 8080"
        echo "  Tailscale URL: http://${TAILSCALE_IP}:8080"
        echo "=========================================="
        echo ""
    else
        echo ""
        echo "=========================================="
        echo "  Blog server running on port 8080"
        echo "  (tailscale ip -4 returned no address)"
        echo "  Local URL:   http://localhost:8080"
        echo "=========================================="
        echo ""
    fi
else
    echo ""
    echo "=========================================="
    echo "  Blog server running on port 8080"
    echo "  (tailscale not found — local only)"
    echo "  Local URL:   http://localhost:8080"
    echo "=========================================="
    echo ""
fi

echo "Press Ctrl-C to stop."
echo ""

# ── Wait for the server process ─────────────────────────────────────────
wait "$SERVER_PID"
