import sys
import yaml
import json
import signal
import asyncio
import argparse

from time import time
from datetime import datetime
from bleak import BleakScanner
from signal import signal, SIGINT

# these things are super accurate; using some swiss made sensor. (tested)
# it's possible to pair - i guess for reading backlog data, but to save energy
# they *openly* broadcast data in the bluetooth manufacturer data header.
# battery status is in service data. which is cool, but privacy issues..
#
# this listens for broadcasts, decodes.
# could filter with bleak, but do it manually by mac.
# only update on a detected sensor change (can get a reading every 5 seconds..)
#
# see https://github.com/hbldh/bleak
# see https://smarthomescene.com/reviews/switchbot-outdoor-thermo-hygrometer-review/
# see https://smarthomescene.com/guides/how-to-integrate-switchbot-outdoor-thermo-hygrometer-in-esphome/
# see https://github.com/OpenWonderLabs/SwitchBotAPI-BLE/blob/latest/devicetypes/meter.md
#
# todo: emit only the thing that has changed as an option.
#
# phil8192@gmail.com | phil@cipherdusk.com


def get_humidity(b: bytes) -> int:
    return b & 0x7F


def get_temp(b1: bytes, b2: bytes) -> float:
    temp_sign = (1 if (b2 & 0x80) > 0 else -1)
    return (b1 & 0x0F) * 0.1 + (b2 & 0x7F) * temp_sign


def get_light(b: bytes) -> int:
    return b & 0x7F


def parse_hub(data: bytes) -> tuple[float, int, int]:
    # first 6 bytes = mac.
    humidity = get_humidity(data[15])
    temp = get_temp(data[13], data[14])
    light = get_light(data[12])
    return temp, humidity, light
    

def parse_sensor(data: bytes) -> tuple[float, int]:
    # first 6 bytes = mac.
    humidity = get_humidity(data[10])
    temp = get_temp(data[8], data[9])
    return temp, humidity


def load_device_info() -> dict:
    with open("devices.yaml", "r") as f:
        sensors = yaml.safe_load(f)
    return sensors


def _pp(
        time: int,
        location: str, 
        id: str, 
        rssi: str, 
        temp: float, 
        humidity: int, 
        light: int | None) -> None:
    time = datetime.fromtimestamp(time) # local time.
    light = f"light level = {light}" if light else ""
    out = (f"{time}\t{location.ljust(15)} {id.ljust(10)} ({str(rssi).rjust(4)}"
           f"dBm)\ttemp = {temp}c humidity = {humidity}% {light}")
    print(out)


def _csv(
        time: int,
        location: str,
        id: str,
        rssi: str,
        temp: float,
        humidity: int,
        light: int | None) -> None:
    light = f"{light}" if light else ""
    print(f"{time},{location},{id},{rssi},{temp},{humidity},{light}")


def _json(
        time: int,
        location: str,
        id: str,
        rssi: str,
        temp: float,
        humidity: int,
        light: int | None) -> None:
    if light is None: del light
    print(f"{json.dumps(locals())}")


def _print(
        time: int,
        output: str,
        location: str,
        id: str,
        rssi: str,
        temp: float,
        humidity: float,
        light: int | None) -> None:
    # delegate
    if output == "pp":
        _pp(time, location, id, rssi, temp, humidity, light)
    elif output == "csv":
        _csv(time, location, id, rssi, temp, humidity, light)
    elif output == "json":
        _json(time, location, id, rssi, temp, humidity, light)
    else:
        print("eh", file=sys.stderr)


async def listen(
        sensors: dict,
        output: str="pp",
        all_readings: bool=False) -> None:

    last_reading = {}
    stop_event = asyncio.Event()
    signal(SIGINT, lambda sig, frame: stop_event.set())

    def callback(device, advertising_data):
        if device.address not in sensors:
            return
        
        m_data = advertising_data.manufacturer_data   
        if len(m_data) != 1:
            print("wtf", file=sys.stderr)
            stop_event.set()
            return
        
        data = list(m_data.values())[0]
        sensor = sensors.get(device.address)
        
        # hub and sensors parsed in diff way.
        light = None
        if sensor["type"] == "hub":
            (temp, humidity, light) = parse_hub(data)
        else:
            (temp, humidity) = parse_sensor(data)

        # if it's a new reading, print it out.
        last = last_reading.get(device.address, ())
        if last != (temp, humidity, light) or all_readings:
            t = int(time()) # utc seconds since epoch.
            last_reading[device.address] = (temp, humidity, light)
            _print(t, output, sensor['location'], sensor['id'], \
                    advertising_data.rssi, temp, humidity, light)    

    async with BleakScanner(callback) as scanner:
        await stop_event.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
            description="read switchbot sensors over bluetooth",
            epilog="will run forever. <crtl-c> to exit.")
    parser.add_argument("-o", "--output",
            help="pretty print (default), (csv or json. so you can |pipe)",
            choices=["pp", "csv", "json"],
            default="pp",
            type=str)
    parser.add_argument("-a", "--all",
            help="print all readings, regardless of state change. \
                  expect every 5 seconds.",
            action="store_true",
            default=False)

    args = parser.parse_args()
    output = args.output
    all_readings = args.all
    sensors = load_device_info()

    asyncio.run(listen(sensors, output, all_readings))
