# Hosting — Tailscale Network Access for the Spectrax Blog

This document covers how to serve the static blog page over your Tailscale network.

## Prerequisites

- **Tailscale** is installed and running on this Linux machine (`tailscale status` should show `active`).
- **Python 3** is available on the host (used by `serve.sh`).
- A `blog/index.html` (or equivalent static files) exists in the `blog/` directory.

## Steps

### 1. Find your Tailscale IP address

```bash
tailscale ip -4
```

This prints the IPv4 address assigned to this machine on your Tailnet (e.g., `100.x.x.x`).

### 2. Start the blog server

```bash
cd /path/to/Spectrax/blog
./serve.sh
```

The script will:
- Start an HTTP server on port **8080** in the `blog/` directory.
- Print the Tailscale URL for easy copy-paste.
- Shut down cleanly when you press **Ctrl-C**.

### 3. Access the blog from another Tailscale device

On any other device connected to the same Tailnet, open a browser and navigate to:

```
http://<tailscale-ip>:8080
```

Replace `<tailscale-ip>` with the address from Step 1.

### 4. (Optional) Use MagicDNS hostname

If your Tailnet has **MagicDNS** enabled, you can use the machine's hostname instead of the IP:

```
http://<hostname>.ts.net:8080
```

Find your hostname with:

```bash
hostname
```

Or check the Tailscale admin console for the machine name.

## Troubleshooting

### Port 8080 is already in use

```
OSError: [Errno 98] Address already in use
```

- Find the process using the port: `lsof -i :8080` or `ss -tlnp | grep 8080`
- Kill it or choose a different port by editing `serve.sh`.

### Tailscale is not running

```
tailscale: command not found
```

or

```
tailscale ip -4 returns no address
```

- Start Tailscale: `sudo systemctl start tailscaled` (or `sudo tailscale up`)
- Verify: `tailscale status`

### Firewall blocking port 8080

Tailscale traffic bypasses the system firewall by default on the tailscale0 interface. If you have a custom firewall rule blocking port 8080:

```bash
# Check for blocking rules (ufw example)
sudo ufw status

# Allow port 8080 on the Tailscale interface
sudo ufw allow in on tailscale0 to any port 8080
```

### Cannot reach the server from another device

1. Verify the server is running (`curl http://localhost:8080` on the host).
2. Verify both devices are on the **same Tailnet** (`tailscale status`).
3. Check that **subnet routing** or **exit nodes** are not interfering.
4. Try the MagicDNS hostname instead of the IP (or vice versa).
