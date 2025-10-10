# ThingsBoard Server Integration with Cloudflare Tunnel

## Overview

This document provides a complete guide for integrating the StamPLC GPS tracker with a ThingsBoard server running in Docker on Fedora, using Cloudflare Tunnel for secure remote access.

## Architecture

```
+-----------------------------------------------------------------+
|                    StamPLC Device (Cellular)                     |
|  +- SIM7080G CatM+GNSS Module                                   |
|  +- Cellular Network (Soracom/AT&T/T-Mobile)                    |
|  +- MQTT Client -> Cloudflare Tunnel -> ThingsBoard Server       |
+-----------------------------------------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                    Cloudflare Tunnel                             |
|  +- Domain: thingsboard.vardanetworks.com                       |
|  +- Protocol: HTTPS (Port 443)                                  |
|  +- TLS/SSL: Automatic (Free)                                   |
|  +- Routing: localhost:8080 (ThingsBoard)                       |
+-----------------------------------------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                   Fedora Server (192.168.86.88)                 |
|  +- ThingsBoard CE (Docker Container)                           |
|  |  +- Web UI: http://localhost:8080                           |
|  |  +- MQTT: tcp://localhost:1883                              |
|  |  +- Database: PostgreSQL (Docker)                           |
|  +- cloudflared (Systemd Service)                               |
|  +- Configuration: /etc/cloudflared/config.yml                  |
+-----------------------------------------------------------------+
```

## System Requirements

### Hardware
- **Fedora Server**: Running ThingsBoard in Docker
- **StamPLC Device**: ESP32-S3 with SIM7080G CatM+GNSS module
- **Network**: Internet connection for both server and device

### Software
- **ThingsBoard CE**: Docker container (Node.js + PostgreSQL)
- **cloudflared**: Cloudflare Tunnel client (installed on Fedora server)
- **StamPLC Firmware**: Custom firmware with MQTT bidirectional commands

## Installation & Setup

### Phase 1: ThingsBoard Server Setup

#### 1.1 Install Docker (if not already installed)

```bash
# Update system
sudo dnf update

# Install Docker
sudo dnf install docker docker-compose

# Start Docker service
sudo systemctl start docker
sudo systemctl enable docker

# Add user to docker group (optional)
sudo usermod -aG docker $USER
```

#### 1.2 Install ThingsBoard via Docker

```bash
# Create directory for ThingsBoard
mkdir -p ~/thingsboard
cd ~/thingsboard

# Create docker-compose.yml
cat > docker-compose.yml << 'EOF'
version: '3.8'
services:
  thingsboard:
    image: thingsboard/tb-node:4.2.0
    container_name: thingsboard
    ports:
      - "8080:8080"    # Web UI
      - "1883:1883"    # MQTT
      - "5683-5688:5683-5688/udp"  # CoAP
    environment:
      - TB_QUEUE_TYPE=in-memory
    volumes:
      - ./data:/data
    depends_on:
      - postgres

  postgres:
    image: postgres:15
    container_name: thingsboard-postgres
    environment:
      - POSTGRES_DB=thingsboard
      - POSTGRES_USER=thingsboard
      - POSTGRES_PASSWORD=postgres
    volumes:
      - ./postgres-data:/var/lib/postgresql/data

volumes:
  postgres-data:
    driver: local
  data:
    driver: local
EOF

# Start ThingsBoard
docker-compose up -d

# Check status
docker ps
```

#### 1.3 Verify ThingsBoard Installation

```bash
# Check container status
docker ps | grep thingsboard

# Test local access
curl -I http://localhost:8080

# Expected: HTTP/1.1 200 OK

# Check logs if needed
docker logs thingsboard
```

### Phase 2: Cloudflare Tunnel Setup

#### 2.1 Install cloudflared

```bash
# Download cloudflared for Linux
wget https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64

# Make executable
chmod +x cloudflared-linux-amd64

# Move to system path
sudo mv cloudflared-linux-amd64 /usr/local/bin/cloudflared

# Verify installation
cloudflared --version
```

#### 2.2 Authenticate with Cloudflare

```bash
# Login to Cloudflare (opens browser)
cloudflared tunnel login

# Creates: ~/.cloudflared/cert.pem
```

#### 2.3 Create Tunnel

```bash
# Create tunnel named "thingsboard"
cloudflared tunnel create thingsboard

# Note the tunnel ID (e.g., 4c108ab2-9e2b-4feb-8a29-e5f13f1c7702)
# Creates: ~/.cloudflared/<tunnel-id>.json
```

#### 2.4 Create Configuration File

```bash
# Create config directory
sudo mkdir -p /etc/cloudflared

# Create config file
sudo nano /etc/cloudflared/config.yml
```

**Configuration Content:**

```yaml
tunnel: 4c108ab2-9e2b-4feb-8a29-e5f13f1c7702  # Replace with your tunnel ID
credentials-file: /home/nik/.cloudflared/4c108ab2-9e2b-4feb-8a29-e5f13f1c7702.json  # Replace with your credentials file path

ingress:
  # ThingsBoard Web UI (HTTP)
  - hostname: thingsboard.vardanetworks.com
    service: http://localhost:8080
    originRequest:
      connectTimeout: 30s
      noTLSVerify: true
  
  # ThingsBoard MQTT (optional)
  - hostname: mqtt.thingsboard.vardanetworks.com
    service: tcp://localhost:1883
  
  # Catch-all rule (keep at end)
  - service: http_status:404
```

#### 2.5 Configure DNS in Cloudflare

1. **Go to**: Cloudflare Dashboard -> Your domain -> DNS
2. **Add CNAME record**:
   - Type: `CNAME`
   - Name: `thingsboard`
   - Target: `<your-tunnel-id>.cfargotunnel.com`
   - Proxy: [x] Proxied (orange cloud)
   - TTL: Auto

#### 2.6 Route DNS to Tunnel

```bash
# Associate DNS with tunnel
cloudflared tunnel route dns thingsboard thingsboard.vardanetworks.com
```

#### 2.7 Install as System Service

```bash
# Install systemd service
sudo cloudflared service install

# Enable auto-start
sudo systemctl enable cloudflared

# Start service
sudo systemctl start cloudflared

# Check status
sudo systemctl status cloudflared
```

### Phase 3: StamPLC Device Configuration

#### 3.1 Get ThingsBoard Access Token

1. **Access ThingsBoard**: `https://thingsboard.vardanetworks.com`
2. **Login**: Default credentials `sysadmin@thingsboard.org` / `sysadmin`
3. **Create Device**:
   - Go to Devices -> Add new device
   - Name: `StamPLC-001`
   - Device profile: Create "StamPLC GPS Tracker" profile
   - Copy the **Access Token** (e.g., `A1B2C3D4E5F6G7H8I9J0`)

#### 3.2 Configure Device Firmware

Add this temporary configuration to [`src/main.cpp`](src/main.cpp):

```cpp
#include <Preferences.h>

void setup() {
    // ============================================================
    // THINGSBOARD CONFIGURATION (ONE-TIME SETUP)
    // ============================================================
    {
        Preferences prefs;
        if (prefs.begin("stamplc", false)) {
            // Cloudflare Tunnel endpoint
            prefs.putString("mHost", "thingsboard.vardanetworks.com");
            prefs.putUInt("mPort", 443);                             // HTTPS port
            prefs.putString("mUser", "YOUR_ACCESS_TOKEN_HERE");      // From ThingsBoard
            prefs.putString("mPass", "");
            
            // Cellular APN
            prefs.putString("apn", "soracom.io");
            prefs.putString("apnU", "sora");
            prefs.putString("apnP", "sora");
            
            prefs.end();
            Serial.println("OKOKOK ThingsBoard settings saved to NVS flash! OKOKOK");
        }
    }
    // ============================================================
    // END OF TEMPORARY CONFIGURATION CODE
    // ============================================================
    
    // ... rest of setup code ...
}
```

#### 3.3 Upload Firmware

```bash
# Upload with configuration
pio run --target upload  # uses default env m5stack-stamps3-freertos
pio device monitor

# Watch for: "OKOKOK ThingsBoard settings saved to NVS flash! OKOKOK"

# Remove configuration code from main.cpp
# Upload again (settings persist in flash)
pio run --target upload  # uses default env m5stack-stamps3-freertos
```

## Configuration Files

### Cloudflare Tunnel Configuration (`/etc/cloudflared/config.yml`)

```yaml
tunnel: 4c108ab2-9e2b-4feb-8a29-e5f13f1c7702
credentials-file: /home/nik/.cloudflared/4c108ab2-9e2b-4feb-8a29-e5f13f1c7702.json

ingress:
  - hostname: thingsboard.vardanetworks.com
    service: http://localhost:8080
    originRequest:
      connectTimeout: 30s
      noTLSVerify: true
  
  - hostname: mqtt.thingsboard.vardanetworks.com
    service: tcp://localhost:1883
  
  - service: http_status:404
```

### StamPLC Device Configuration (NVS Storage)

| Key | Value | Description |
|-----|-------|-------------|
| `mHost` | `thingsboard.vardanetworks.com` | Cloudflare domain |
| `mPort` | `443` | HTTPS port |
| `mUser` | `A1B2C3D4E5F6G7H8I9J0` | ThingsBoard access token |
| `mPass` | `` | Empty for ThingsBoard |
| `apn` | `soracom.io` | Cellular APN |
| `apnU` | `sora` | APN username |
| `apnP` | `sora` | APN password |

## Network Architecture

### Data Flow

```
StamPLC Device (Cellular)
       v
Cellular Network (CGNAT)
       v
Internet (Public)
       v
Cloudflare Edge Network
       v
thingsboard.vardanetworks.com
       v
Fedora Server (192.168.86.88:8080)
       v
ThingsBoard Docker Container
```

### Port Mapping

| Protocol | External | Internal | Description |
|----------|----------|----------|-------------|
| HTTPS | 443 | 8080 | ThingsBoard Web UI |
| MQTT | 1883 | 1883 | MQTT Broker |
| CoAP | 5683-5688 | 5683-5688 | CoAP Protocol |

## Troubleshooting

### Cloudflare Tunnel Issues

#### Symptom: "control stream encountered a failure"
**Cause**: Service configuration mismatch

**Solution**:
```bash
# Check service configuration
sudo systemctl cat cloudflared

# Fix service to use config file
sudo systemctl edit cloudflared
# Add:
[Service]
ExecStart=
ExecStart=/usr/bin/cloudflared --config /etc/cloudflared/config.yml tunnel run

# Apply changes
sudo systemctl daemon-reload
sudo systemctl restart cloudflared
```

#### Symptom: Tunnel connects but no traffic
**Cause**: ThingsBoard not accessible locally

**Solution**:
```bash
# Test local access
curl -I http://localhost:8080

# Check Docker container
docker ps | grep thingsboard

# Check container logs
docker logs thingsboard
```

### DNS Issues

#### Symptom: Domain not resolving
**Cause**: DNS not propagated or misconfigured

**Solution**:
```bash
# Check DNS resolution
nslookup thingsboard.vardanetworks.com

# Check Cloudflare DNS
dig thingsboard.vardanetworks.com

# Verify tunnel routes
cloudflared tunnel route dns list
```

### Device Connection Issues

#### Symptom: Device cannot connect
**Cause**: Wrong port or protocol

**Solution**:
- Use port **443** (HTTPS) for Cloudflare Tunnel
- Verify access token is correct
- Check cellular signal strength

## Monitoring & Maintenance

### Cloudflare Tunnel Monitoring

```bash
# Check tunnel status
cloudflared tunnel info thingsboard

# View service logs
sudo journalctl -u cloudflared -f

# Check tunnel metrics
curl http://localhost:20241/metrics
```

### ThingsBoard Monitoring

```bash
# Check container status
docker ps | grep thingsboard

# View application logs
docker logs -f thingsboard

# Check database status
docker logs -f thingsboard-postgres
```

### StamPLC Device Monitoring

```bash
# Monitor serial output
pio device monitor

# Watch for these messages:
[CATM_GNSS_TASK] Cellular attach succeeded
[MQTT] MQTT connected host=thingsboard.vardanetworks.com:443
[MQTT] MQTT subscribed to v1/devices/me/rpc/request/+
```

## Security Considerations

### Cloudflare Tunnel Security
- [x] **End-to-end encryption** (TLS 1.3)
- [x] **No public IP exposure**
- [x] **Automatic certificate management**
- [x] **DDoS protection** via Cloudflare

### ThingsBoard Security
- [x] **Access token authentication**
- [x] **MQTT over TLS** (when configured)
- [x] **User role management**
- [x] **Audit logging**

### Network Security
- [x] **No port forwarding** required
- [x] **Firewall friendly** (only outbound connections)
- [x] **Private network isolation**

## Performance Optimization

### Cloudflare Tunnel Optimization

```yaml
# Enhanced configuration for better performance
ingress:
  - hostname: thingsboard.vardanetworks.com
    service: http://localhost:8080
    originRequest:
      connectTimeout: 30s
      tlsTimeout: 30s
      tcpKeepAlive: 30s
      keepAliveConnections: 100
      keepAliveTimeout: 90s
      httpHostHeader: localhost:8080
```

### ThingsBoard Performance

```bash
# Monitor resource usage
docker stats thingsboard

# Check Java heap usage
docker exec thingsboard jcmd 1 VM.flags
```

## Backup & Recovery

### ThingsBoard Data Backup

```bash
# Stop ThingsBoard
docker-compose down

# Backup database
docker run --rm -v thingsboard_postgres-data:/data -v $(pwd):/backup alpine tar czf /backup/postgres-backup.tar.gz /data

# Backup ThingsBoard data
docker run --rm -v thingsboard_data:/data -v $(pwd):/backup alpine tar czf /backup/thingsboard-backup.tar.gz /data

# Restart ThingsBoard
docker-compose up -d
```

### Cloudflare Tunnel Backup

```bash
# Backup tunnel credentials
cp ~/.cloudflared/*.json /secure/backup/location/

# Backup configuration
cp /etc/cloudflared/config.yml /secure/backup/location/
```

## Cost Analysis

### Infrastructure Costs

| Component | Cost | Notes |
|-----------|------|-------|
| **Fedora Server** | $0 | Existing hardware |
| **ThingsBoard CE** | $0 | Free open source |
| **Cloudflare Tunnel** | $0 | Free tier |
| **Domain (vardanetworks.com)** | $12/year | Existing domain |
| **Cellular Data (Soracom)** | $1-5/month | Per device |

### Total Cost: **$1-5/month per device**

## Scaling Considerations

### Multiple Devices

For multiple StamPLC devices:

1. **Create additional devices** in ThingsBoard
2. **Each device gets unique access token**
3. **Configure each device** with its token
4. **Use device-specific topics** for organization

### High Availability

For production deployment:

1. **Multiple tunnel instances** across regions
2. **ThingsBoard clustering** for redundancy
3. **Database replication** for data safety
4. **Load balancing** for web interface

## API Integration

### ThingsBoard REST API

```bash
# Get device telemetry
curl -H "Authorization: Bearer YOUR_ACCESS_TOKEN" \
  https://thingsboard.vardanetworks.com/api/plugins/telemetry/DEVICE/STAMPLE-001/values/timeseries

# Send RPC command
curl -X POST \
  -H "Authorization: Bearer YOUR_ACCESS_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"method": "get_gps", "params": {}}' \
  https://thingsboard.vardanetworks.com/api/plugins/rpc/oneway/STAMPLE-001
```

### MQTT API (Device Side)

```json
// Device publishes telemetry
{
  "latitude": 37.7749,
  "longitude": -122.4194,
  "altitude": 15.5,
  "satellites": 8,
  "signal_strength": -75
}

// Server sends RPC command
{
  "method": "get_gps",
  "params": {}
}
```

## Testing Procedures

### 1. Local Testing

```bash
# Test ThingsBoard locally
curl -I http://localhost:8080

# Test MQTT locally
mosquitto_sub -h localhost -p 1883 -t "test"
```

### 2. Tunnel Testing

```bash
# Test tunnel connectivity
curl -I https://thingsboard.vardanetworks.com

# Test MQTT through tunnel
mosquitto_sub -h thingsboard.vardanetworks.com -p 1883 -t "test"
```

### 3. Device Testing

```bash
# Monitor device connection
pio device monitor

# Watch for:
[MQTT] MQTT connected host=thingsboard.vardanetworks.com:443
[MQTT] MQTT publish success
```

## Common Issues & Solutions

### Issue: Tunnel Connection Fails

**Symptoms:**
```
ERR control stream encountered a failure
ERR Retrying connection
```

**Solutions:**
1. **Check ThingsBoard status**: `docker ps | grep thingsboard`
2. **Test local access**: `curl -I http://localhost:8080`
3. **Restart tunnel**: `sudo systemctl restart cloudflared`
4. **Check logs**: `sudo journalctl -u cloudflared -n 50`

### Issue: DNS Not Resolving

**Symptoms:**
```
nslookup thingsboard.vardanetworks.com
;; connection timed out; no servers could be reached
```

**Solutions:**
1. **Wait for propagation**: DNS can take 5-30 minutes
2. **Check Cloudflare DNS**: `dig @1.1.1.1 thingsboard.vardanetworks.com`
3. **Verify CNAME record** in Cloudflare dashboard

### Issue: Device Cannot Connect

**Symptoms:**
```
[MQTT] MQTT connect fail
```

**Solutions:**
1. **Verify access token** in ThingsBoard device page
2. **Check domain spelling**: `thingsboard.vardanetworks.com`
3. **Confirm port 443** (not 1883)
4. **Check cellular signal**: Should be > -100 dBm

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-01-09 | Initial implementation |
| 1.0.1 | 2025-01-09 | Fixed systemd service configuration |
| 1.0.2 | 2025-01-09 | Added troubleshooting section |

## Support Resources

- **ThingsBoard Documentation**: https://thingsboard.io/docs/
- **Cloudflare Tunnel Docs**: https://developers.cloudflare.com/cloudflare-one/connections/connect-apps/
- **StamPLC MQTT Commands**: [`MQTT_BIDIRECTIONAL_COMMANDS.md`](MQTT_BIDIRECTIONAL_COMMANDS.md)
- **Quick Start Guide**: [`MQTT_COMMANDS_QUICK_START.md`](MQTT_COMMANDS_QUICK_START.md)

## Conclusion

This setup provides a **secure, scalable, and cost-effective** solution for remote GPS tracking with:

- [x] **Secure remote access** via Cloudflare Tunnel
- [x] **Professional domain** (thingsboard.vardanetworks.com)
- [x] **Automatic HTTPS/SSL** encryption
- [x] **No port forwarding** required
- [x] **Cellular device compatibility**
- [x] **Bidirectional MQTT commands**
- [x] **Real-time dashboards**
- [x] **OTA update capability**

The system is **production-ready** and can scale to multiple devices with minimal additional configuration.

---

**Ready for deployment! Your StamPLC devices can now connect securely from anywhere via cellular to your ThingsBoard server.**
