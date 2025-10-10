#!/usr/bin/env python3
"""
MQTT Command Client for StamPLC GPS Tracker
Sends commands and receives responses from the device
"""

import paho.mqtt.client as mqtt
import json
import time
import argparse

# Configuration defaults (matches device NVS defaults)
BROKER = "thingsboard.vardanetworks.com"
PORT = 1883
USERNAME = "lfew095hy4q1adoig02i"
PASSWORD = ""

class StamPLCClient:
    def __init__(self, device_id, broker=BROKER, port=PORT, username=None, password=None):
        self.device_id = device_id
        self.broker = broker
        self.port = port
        
        # Topics
        self.rpc_request_prefix = "v1/devices/me/rpc/request"
        self.rpc_response_topic = "v1/devices/me/rpc/response/+"
        self.telemetry_topic = "v1/devices/me/telemetry"
        self.attributes_topic = "v1/devices/me/attributes"
        
        # MQTT Client
        self.client = mqtt.Client(client_id=f"stamplc-client-{int(time.time())}")
        
        if username and password:
            self.client.username_pw_set(username, password)
        
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        
        self.pending_requests = {}
        self.connected = False
    
    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT broker: {self.broker}:{self.port}")
            self.connected = True
            
            # Subscribe to all device topics
            client.subscribe(self.rpc_response_topic)
            client.subscribe(self.telemetry_topic)
            client.subscribe(self.attributes_topic)
            
            print("Subscribed to RPC responses, telemetry, and attributes")
        else:
            print(f"Connection failed with code: {rc}")
    
    def _on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload.decode())
            
            if msg.topic.startswith("v1/devices/me/rpc/response/"):
                print(f"\n[RESPONSE] {data}")
                
                # Handle pending request
                if 'id' in data and data['id'] in self.pending_requests:
                    self.pending_requests[data['id']] = data
            
            elif msg.topic == self.telemetry_topic:
                print(f"\n[TELEMETRY] {json.dumps(data, indent=2)}")
            
            elif msg.topic == self.attributes_topic:
                print(f"\n[ATTRIBUTES] {json.dumps(data, indent=2)}")
        
        except json.JSONDecodeError:
            print(f"[ERROR] Failed to parse JSON: {msg.payload}")
        except Exception as e:
            print(f"[ERROR] {e}")
    
    def connect(self):
        """Connect to MQTT broker"""
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            
            # Wait for connection
            timeout = 10
            while not self.connected and timeout > 0:
                time.sleep(0.5)
                timeout -= 0.5
            
            if not self.connected:
                print("Failed to connect within timeout")
                return False
            
            return True
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from MQTT broker"""
        self.client.loop_stop()
        self.client.disconnect()
    
    def send_command(self, cmd_type, params=None, wait_response=True, timeout=30):
        """Send a command to the device"""
        if not self.connected:
            print("Not connected to broker")
            return None
        
        request_id = f"req-{int(time.time() * 1000)}"
        message = {
            "method": cmd_type,
            "params": params or {}
        }
        
        # Mark as pending
        if wait_response:
            self.pending_requests[request_id] = None
        
        # Publish command
        rpc_topic = f"{self.rpc_request_prefix}/{request_id}"
        self.client.publish(rpc_topic, json.dumps(message))
        print(f"[SENT] Command: {cmd_type}, ID: {request_id}")
        
        # Wait for response
        if wait_response:
            start_time = time.time()
            while time.time() - start_time < timeout:
                if self.pending_requests[request_id] is not None:
                    response = self.pending_requests[request_id]
                    del self.pending_requests[request_id]
                    return response
                time.sleep(0.1)
            
            # Timeout
            del self.pending_requests[request_id]
            print(f"[TIMEOUT] No response received for {request_id}")
            return None
        
        return {"status": "sent"}
    
    def get_gps(self):
        """Request current GPS position"""
        return self.send_command("get_gps")
    
    def get_stats(self):
        """Request device statistics"""
        return self.send_command("get_stats")
    
    def get_logs(self, lines=50, log_type="all"):
        """Request recent logs"""
        return self.send_command("get_logs", {
            "lines": lines,
            "type": log_type
        })
    
    def update_config(self, config):
        """Update device configuration"""
        return self.send_command("config_update", config)
    
    def ota_update(self, firmware_url, version, md5=None):
        """Trigger OTA firmware update"""
        params = {
            "url": firmware_url,
            "version": version
        }
        if md5:
            params["md5"] = md5
        
        return self.send_command("ota_update", params, timeout=120)
    
    def reboot(self, delay_ms=5000):
        """Reboot the device"""
        return self.send_command("reboot", {"delay_ms": delay_ms})
    
    def set_interval(self, interval_type, value_ms):
        """Set reporting interval"""
        return self.send_command("set_interval", {
            "type": interval_type,
            "value_ms": value_ms
        })


def main():
    parser = argparse.ArgumentParser(description='StamPLC MQTT Command Client')
    parser.add_argument('device_id', help='Device ID (MAC address)')
    parser.add_argument('--broker', default=BROKER, help='MQTT broker address')
    parser.add_argument('--port', type=int, default=PORT, help='MQTT broker port')
    parser.add_argument('--username', help='MQTT username')
    parser.add_argument('--password', help='MQTT password')
    
    subparsers = parser.add_subparsers(dest='command', help='Command to send')
    
    # GPS command
    subparsers.add_parser('gps', help='Get current GPS position')
    
    # Stats command
    subparsers.add_parser('stats', help='Get device statistics')
    
    # Logs command
    logs_parser = subparsers.add_parser('logs', help='Get device logs')
    logs_parser.add_argument('--lines', type=int, default=50, help='Number of lines')
    
    # Config update command
    config_parser = subparsers.add_parser('config', help='Update configuration')
    config_parser.add_argument('--gps-interval', type=int, help='GPS interval (ms)')
    config_parser.add_argument('--mqtt-host', help='MQTT broker host')
    config_parser.add_argument('--mqtt-port', type=int, help='MQTT broker port')
    config_parser.add_argument('--apn', help='Cellular APN')
    
    # OTA update command
    ota_parser = subparsers.add_parser('ota', help='Perform OTA firmware update')
    ota_parser.add_argument('url', help='Firmware URL')
    ota_parser.add_argument('version', help='Firmware version')
    ota_parser.add_argument('--md5', help='Firmware MD5 hash')
    
    # Reboot command
    reboot_parser = subparsers.add_parser('reboot', help='Reboot device')
    reboot_parser.add_argument('--delay', type=int, default=5000, help='Delay before reboot (ms)')
    
    # Set interval command
    interval_parser = subparsers.add_parser('interval', help='Set reporting interval')
    interval_parser.add_argument('type', choices=['gps_publish', 'stats_publish'], help='Interval type')
    interval_parser.add_argument('value', type=int, help='Interval value (ms)')
    
    # Monitor mode
    subparsers.add_parser('monitor', help='Monitor all device messages')
    
    args = parser.parse_args()
    
    # Create client
    client = StamPLCClient(
        args.device_id,
        broker=args.broker,
        port=args.port,
        username=args.username,
        password=args.password
    )
    
    # Connect
    if not client.connect():
        return 1
    
    try:
        # Execute command
        if args.command == 'gps':
            response = client.get_gps()
            if response:
                print(f"\nGPS Position:")
                print(f"  Latitude:  {response['data']['latitude']}")
                print(f"  Longitude: {response['data']['longitude']}")
                print(f"  Altitude:  {response['data']['altitude']} m")
                print(f"  Satellites: {response['data']['satellites']}")
                print(f"  Valid:     {response['data']['valid']}")
        
        elif args.command == 'stats':
            response = client.get_stats()
            if response:
                print(f"\nDevice Statistics:")
                print(json.dumps(response['data'], indent=2))
        
        elif args.command == 'logs':
            response = client.get_logs(lines=args.lines)
            if response:
                print(f"\nDevice Logs:")
                print(response['data']['logs'])
        
        elif args.command == 'config':
            config = {}
            if args.gps_interval:
                config['gps_interval'] = args.gps_interval
            if args.mqtt_host:
                config['mqtt_host'] = args.mqtt_host
            if args.mqtt_port:
                config['mqtt_port'] = args.mqtt_port
            if args.apn:
                config['apn'] = args.apn
            
            if config:
                response = client.update_config(config)
                if response:
                    print(f"\nConfiguration Updated:")
                    print(f"  Updated fields: {response['data']['updated']}")
                    print(f"  Restart required: {response['data']['restart_required']}")
        
        elif args.command == 'ota':
            print(f"\nStarting OTA update...")
            print(f"  URL: {args.url}")
            print(f"  Version: {args.version}")
            response = client.ota_update(args.url, args.version, args.md5)
            if response:
                print(f"\nOTA Update Result:")
                print(f"  Status: {response['status']}")
                if response['status'] == 'success':
                    print(f"  Device will reboot shortly")
        
        elif args.command == 'reboot':
            response = client.reboot(delay_ms=args.delay)
            if response:
                print(f"\nReboot scheduled: {response['data']['message']}")
        
        elif args.command == 'interval':
            response = client.set_interval(args.type, args.value)
            if response:
                print(f"\nInterval Updated:")
                print(f"  Type: {args.type}")
                print(f"  Previous: {response['data']['previous_value']} ms")
                print(f"  New: {response['data']['new_value']} ms")
        
        elif args.command == 'monitor':
            print("\nMonitoring device messages... (Press Ctrl+C to stop)")
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\nStopping monitor...")
        
        else:
            print("No command specified. Use --help for usage.")
    
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        client.disconnect()
    
    return 0


if __name__ == "__main__":
    exit(main())
