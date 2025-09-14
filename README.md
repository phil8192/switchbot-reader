# switchbot reader

reading data from switchbot sensors.

## synopsis
```
[phil@arasaka switchbot]$ python switchbot.py -h
usage: switchbot.py [-h] [-o {pp,csv,json}] [-a]

read switchbot sensors over bluetooth

options:
  -h, --help            show this help message and exit
  -o, --output {pp,csv,json}
                        pretty print (default), (csv or json. so you can |pipe)
  -a, --all             print all readings, regardless of state change. expect every 5 seconds.

will run forever. <crtl-c> to exit.
```

## conf

```cp example-devices.yaml devices.yaml```
...edit.

## examples

### pretty print
just watch.

```bash
[phil@arasaka switchbot]$ python switchbot.py
2025-09-13 11:00:24	office          sensor-1   ( -42dBm)	temp = 21.7c humidity = 62% 
2025-09-13 11:00:25	living room     sensor-0   ( -78dBm)	temp = 19.4c humidity = 61% light level = 1
2025-09-13 11:00:25	attic           sensor-8   ( -76dBm)	temp = 18.4c humidity = 81% 
2025-09-13 11:00:26	garden          sensor-6   ( -86dBm)	temp = 14.7c humidity = 79% 
2025-09-13 11:00:26	garden          sensor-7   ( -78dBm)	temp = 14.9c humidity = 77% 
2025-09-13 11:00:30	kitchen         sensor-3   ( -82dBm)	temp = 18.8c humidity = 64%
2025-09-13 11:02:42	garden          sensor-6   ( -84dBm)	temp = 14.4c humidity = 81% 
2025-09-13 11:03:26	garden          sensor-7   ( -80dBm)	temp = 14.5c humidity = 77% 
2025-09-13 11:04:47	garden          sensor-6   ( -88dBm)	temp = 14.1c humidity = 81% 
2025-09-13 11:05:14	garden          sensor-7   ( -82dBm)	temp = 14.1c humidity = 78% 
2025-09-13 11:06:46	attic           sensor-8   ( -76dBm)	temp = 18.5c humidity = 78% 
2025-09-13 11:07:29	garden          sensor-6   ( -94dBm)	temp = 13.8c humidity = 81% 
2025-09-13 11:08:07	garden          sensor-7   ( -80dBm)	temp = 13.8c humidity = 78% 
2025-09-13 11:09:18	garden          sensor-6   ( -78dBm)	temp = 13.5c humidity = 82% 
2025-09-13 11:10:59	garden          sensor-7   ( -96dBm)	temp = 13.5c humidity = 80% 
2025-09-13 11:13:37	kitchen         sensor-3   ( -90dBm)	temp = 18.7c humidity = 64% 
2025-09-13 11:14:44	garden          sensor-6   ( -82dBm)	temp = 13.2c humidity = 82% 
2025-09-13 11:15:28	garden          sensor-7   ( -90dBm)	temp = 13.2c humidity = 80% 
2025-09-13 11:15:59	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 2
2025-09-13 11:16:54	attic           sensor-8   ( -86dBm)	temp = 18.3c humidity = 77% 
2025-09-13 11:16:54	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 3
2025-09-13 11:17:14	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 4
2025-09-13 11:17:26	living room     sensor-0   ( -82dBm)	temp = 19.4c humidity = 61% light level = 5
2025-09-13 11:17:38	living room     sensor-0   ( -88dBm)	temp = 19.4c humidity = 61% light level = 8
2025-09-13 11:17:47	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 6
2025-09-13 11:17:58	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 10
2025-09-13 11:18:08	living room     sensor-0   ( -88dBm)	temp = 19.4c humidity = 61% light level = 11
2025-09-13 11:18:30	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 12
2025-09-13 11:21:00	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 11
2025-09-13 11:21:22	living room     sensor-0   ( -80dBm)	temp = 19.4c humidity = 61% light level = 12
2025-09-13 11:21:58	garden          sensor-7   ( -74dBm)	temp = 13.6c humidity = 82%
...
```

### json
pipe it to something.

```bash
[phil@arasaka switchbot]$ python -u switchbot.py -a -o json |jq
{
  "time": 1757762346,
  "location": "garden",
  "id": "sensor-6",
  "rssi": -80,
  "temp": 14.0,
  "humidity": 82
}
{
  "time": 1757762400,
  "location": "office",
  "id": "sensor-1",
  "rssi": -46,
  "temp": 21.7,
  "humidity": 62
}
...
```

### csv
dump it to something.

```bash
[phil@arasaka switchbot]$ python -u switchbot.py -o csv >/tmp/dump.csv
[phil@arasaka switchbot]$ cat /tmp/dump.csv 
1757762656,living room,sensor-0,-86,19.6,61,3
1757762659,garden,sensor-7,-80,15.7,75,
1757762659,office,sensor-1,-46,21.7,62,
1757762659,garden,sensor-6,-82,14.2,84,
...
```

### pipe to a dashboard
example ai generated.
```bash
SB_STALE_SECS=300 python -u switchbot.py -a -o json | tools/ai_generated_console_dash_ncurses
```
