import zmq
import json
import time

context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.connect("tcp://localhost:4040")

data = {
    "accuracy": 15.2,
    "latitude": 55.04502333333333,
    "longitude": 82.98290166666666,
    "provider": "gps",
    "recordedTime": int(time.time() * 1000),
    "source": "sensor",
    "timestamp": int(time.time() * 1000)
}

socket.send_json(data)
print("Сообщение отправлено!")