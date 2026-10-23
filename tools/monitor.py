import argparse,time
from pathlib import Path
import serial
p=argparse.ArgumentParser();p.add_argument('--port',default='COM12');p.add_argument('--seconds',type=int,default=15);p.add_argument('--output',default='build/runtime.log');a=p.parse_args()
s=serial.Serial();s.port=a.port;s.baudrate=115200;s.timeout=.3;s.dtr=False;s.rts=False;s.open()
end=time.monotonic()+a.seconds;parts=[]
try:
    while time.monotonic()<end:
        raw=s.read(4096)
        if raw:parts.append(raw)
finally:s.close()
text=b''.join(parts).decode('utf-8',errors='replace');Path(a.output).write_text(text,encoding='utf-8')
for line in text.splitlines():
    if any(k in line.lower() for k in ['error','panic','assert','abort','failed','overflow','newsboard','got ip','ip:','rst:','backtrace']):print(line)
print(f'Captured {len(text)} bytes to {a.output}')
