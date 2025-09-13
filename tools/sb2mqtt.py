#!/usr/bin/env python3
"""
Read JSON lines from stdin (produced by `python -u switchbot.py -a -o json`)
and forward each reading to MQTT (and optionally InfluxDB line protocol).

Usage:
  python -u switchbot.py -a -o json | python tools/sb2mqtt.py --mqtt-host 127.0.0.1 --mqtt-retain --influx-file -
"""
from __future__ import annotations
import argparse, json, os, sys
from sinks import build_sink

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser("sb2mqtt")
    # MQTT
    p.add_argument("--mqtt-host", default=os.getenv("MQTT_HOST","127.0.0.1"))
    p.add_argument("--mqtt-port", type=int, default=int(os.getenv("MQTT_PORT","1883")))
    p.add_argument("--mqtt-username", default=os.getenv("MQTT_USERNAME"))
    p.add_argument("--mqtt-password", default=os.getenv("MQTT_PASSWORD"))
    p.add_argument("--mqtt-topic", default=os.getenv("MQTT_TOPIC","home/sensors"))
    p.add_argument("--mqtt-retain", action="store_true")
    # Influx
    p.add_argument("--influx-file", default="", help="Write Influx line protocol to file (or '-' for stdout)")
    return p

def main(argv=None) -> int:
    args = build_parser().parse_args(argv)

    mqtt_sink = build_sink(
        "mqtt",
        host=args.mqtt_host, port=args.mqtt_port,
        username=args.mqtt_username, password=args.mqtt_password,
        base_topic=args.mqtt_topic, retain=args.mqtt_retain,
    )
    influx_sink = build_sink("influx", influx_file=args.influx_file) if args.influx_file else None

    mqtt_sink.open()
    if influx_sink: influx_sink.open()

    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                reading = json.loads(line)
            except Exception:
                # pass through any non-JSON (logs) to stderr
                print(line, file=sys.stderr)
                continue

            mqtt_sink.write(reading)
            if influx_sink:
                influx_sink.write(reading)
    finally:
        mqtt_sink.close()
        if influx_sink: influx_sink.close()

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
