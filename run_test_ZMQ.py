
import zmq
import json
import time

context = zmq.Context()
socket = context.socket(zmq.PUSH)

socket.connect("tcp://157.22.179.103:5555") 

data = {
    "accuracy": 15.2,
    "latitude": 56.0003, # Поставь те, что видны на карте сейчас
    "longitude": 89.0034,
    "provider": "gps",
    "recordedTime": int(time.time() * 1000),
    "source": "sensor",
    "timestamp": int(time.time() * 1000)
}

socket.send_json(data)
print("Пакет отправлен в очередь. Ожидаем отправки в сеть...")
time.sleep(1.5) # ОБЯЗАТЕЛЬНО! Дает время вытолкнуть данные из буфера
socket.close()
context.term()
print("Готово.")