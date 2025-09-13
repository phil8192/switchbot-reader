from __future__ import annotations
"""
Lightweight sinks for switchbot.py:
  - PrettySink / CsvSink / JsonSink (stdout)
  - MqttSink (publish retained JSON)
  - InfluxSink (line protocol -> stdout/file)
Use with:
  python -u switchbot.py -a -o json | python tools/sb2mqtt.py ...
"""
import csv, json, sys, time
from typing import Any, Dict, Optional, TextIO
try:
    import paho.mqtt.client as mqtt  # optional
except Exception:
    mqtt = None

class Sink:
    def open(self) -> None: ...
    def write(self, reading: Dict[str, Any]) -> None: ...
    def close(self) -> None: ...

class PrettySink(Sink):
    def __init__(self, stream: TextIO = sys.stdout) -> None:
        self.stream = stream
    def open(self) -> None: ...
    def write(self, reading: Dict[str, Any]) -> None:
        self.stream.write(json.dumps(reading, indent=2) + "\n")
        self.stream.flush()
    def close(self) -> None: ...

class CsvSink(Sink):
    def __init__(self, stream: TextIO = sys.stdout) -> None:
        self.stream = stream
        self._writer = None
        self._fields = None
    def open(self) -> None: ...
    def write(self, reading: Dict[str, Any]) -> None:
        if self._writer is None:
            self._fields = list(reading.keys())
            self._writer = csv.DictWriter(self.stream, fieldnames=self._fields)
            self._writer.writeheader()
        self._writer.writerow({k: reading.get(k) for k in self._fields})
    def close(self) -> None: ...

class JsonSink(Sink):
    def __init__(self, stream: TextIO = sys.stdout) -> None:
        self.stream = stream
    def open(self) -> None: ...
    def write(self, reading: Dict[str, Any]) -> None:
        self.stream.write(json.dumps(reading) + "\n")
        self.stream.flush()
    def close(self) -> None: ...

def _esc(s: str) -> str:
    return str(s).replace(" ", "\\ ").replace(",", "\\,").replace("=", "\\=")

class InfluxSink(Sink):
    def __init__(self, filepath: str = "-") -> None:
        self.filepath = filepath
        self.stream: Optional[TextIO] = None
    def open(self) -> None:
        if self.filepath in ("", "-"):
            self.stream = sys.stdout
        else:
            self.stream = open(self.filepath, "a", buffering=1)
    def write(self, reading: Dict[str, Any]) -> None:
        assert self.stream is not None
        room = _esc(reading.get("location","unknown"))
        dev  = _esc(reading.get("id","unknown"))
        tags = f"room={room},device={dev}"
        fields = []
        mapping = {"temp":"temperature_c","humidity":"humidity_pct","light":"light","rssi":"rssi_dbm","co2_ppm":"co2_ppm","voc_index":"voc_index"}
        for src, dst in mapping.items():
            v = reading.get(src)
            if v is None:
                continue
            if isinstance(v, int) and dst not in ("temperature_c", "voc_index"):
                fields.append(f"{dst}={v}i")
            else:
                try:
                    fields.append(f"{dst}={float(v)}")
                except Exception:
                    pass
        if not fields:
            return
        ts_ns = int(reading.get("time", time.time())) * 1_000_000_000
        line = f"env,{tags} " + ",".join(fields) + f" {ts_ns}\n"
        self.stream.write(line)
        self.stream.flush()
    def close(self) -> None:
        if self.stream and self.stream is not sys.stdout:
            self.stream.close()
        self.stream = None

class MqttSink(Sink):
    def __init__(self, host: str, port: int = 1883, username: Optional[str] = None, password: Optional[str] = None,
                 base_topic: str = "home/sensors", retain: bool = True) -> None:
        if mqtt is None:
            raise RuntimeError("paho-mqtt not installed; run: pip install paho-mqtt")
        self.host, self.port = host, port
        self.username, self.password = username, password
        self.base_topic = base_topic.rstrip("/")
        self.retain = retain
        self._client: Optional[mqtt.Client] = None
    def open(self) -> None:
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if self.username:
            self._client.username_pw_set(self.username, self.password)
        self._client.connect(self.host, self.port, keepalive=60)
        self._client.loop_start()
    def write(self, reading: Dict[str, Any]) -> None:
        assert self._client is not None
        room = (reading.get("location","unknown")).replace(" ", "_").lower()
        dev  = (reading.get("id","unknown")).replace(":", "").upper()
        topic = f"{self.base_topic}/{room}/{dev}/state"
        enriched = dict(reading)
        enriched.setdefault("ts", int(time.time()))
        enriched.setdefault("source", "ble")
        payload = json.dumps(enriched, separators=(",",":"))
        self._client.publish(topic, payload, retain=self.retain)
    def close(self) -> None:
        if self._client:
            self._client.loop_stop()
            self._client.disconnect()
            self._client = None

def build_sink(kind: str, **kwargs) -> Sink:
    if kind == "pp":   return PrettySink()
    if kind == "csv":  return CsvSink()
    if kind == "json": return JsonSink()
    if kind == "mqtt": return MqttSink(**kwargs)
    if kind == "influx": return InfluxSink(kwargs.get("influx_file","-"))
    raise ValueError(f"unknown sink kind: {kind}")
