"""No microphone/model. Exercise QProcess JSONL framing/acknowledgement only."""
import json
import sys
import time


def emit(event, **fields):
    print(json.dumps(dict(protocol="longpet-kws", version=1, event=event, **fields)), flush=True)


time.sleep(.08)
emit("ready", paused=True)
for line in sys.stdin:
    data = json.loads(line)
    command = data["command"]
    command_id = data["command_id"]
    if command == "stop":
        emit("stopped", command_id=command_id)
        break
    if command == "pause":
        emit("paused", command_id=command_id - 1)  # Deliberate stale ack.
        emit("paused", command_id=command_id)
    if command == "resume":
        emit("resumed", command_id=command_id)
