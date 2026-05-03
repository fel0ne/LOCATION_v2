import json
import os
import sys
import re

def parse_cell_info(cell_str):
    data = {
        "identity": {"mCi": 0, "mPci": 0, "mTac": 0, "mEarfcn": 0, "mMcc": "250", "mMnc": "01"},
        "signal": {"rsrp": -110, "rsrq": -15, "rssnr": 0, "ta": 0}
    }
    if not cell_str or not isinstance(cell_str, str):
        return data

    patterns = {
        "mCi": r"mCi=(\d+)",
        "mPci": r"mPci=(\d+)",
        "mTac": r"mTac=(\d+)",
        "mEarfcn": r"mEarfcn=(\d+)",
        "mMcc": r"mMcc=(\d+)",
        "mMnc": r"mMnc=(\d+)",
        "rsrp": r"rsrp=(-?\d+)",
        "rsrq": r"rsrq=(-?\d+)",
        "rssnr": r"rssnr=(-?\d+)",
        "ta": r"ta=(\d+)"
    }

    for key in data["identity"]:
        match = re.search(patterns[key], cell_str)
        if match:
            val = match.group(1)
            data["identity"][key] = val if key in ["mMcc", "mMnc"] else int(val)

    for key in data["signal"]:
        match = re.search(patterns[key], cell_str)
        if match:
            val = int(match.group(1))
            if val == 2147483647:
                val = 0 if key in ["rssnr", "ta"] else data["signal"][key]
            data["signal"][key] = val
    return data

def transform_data(item):
    if "error" in item or "longitude" not in item:
        return None

    cell_data = parse_cell_info(item.get("cell", ""))
    
    return {
        "accuracy": item.get("accuracy", 100.0),
        "latitude": item.get("latitude", 0.0),
        "longitude": item.get("longitude", 0.0),
        "provider": "gps",
        "recordedTime": item.get("time"),
        "source": "friend_log_v2",
        "telephony": {
            "LTE": {
                "identity": {
                    "band": 0,
                    "ci": cell_data["identity"]["mCi"],
                    "earfcn": cell_data["identity"]["mEarfcn"],
                    "mcc": cell_data["identity"]["mMcc"],
                    "mnc": cell_data["identity"]["mMnc"],
                    "pci": cell_data["identity"]["mPci"],
                    "tac": cell_data["identity"]["mTac"]
                },
                "signal": {
                    "asu": 0,
                    "rsrp": cell_data["signal"]["rsrp"],
                    "rsrq": cell_data["signal"]["rsrq"],
                    "rssnr": cell_data["signal"]["rssnr"],
                    "ta": cell_data["signal"]["ta"]
                }
            }
        },
        "timestamp": item.get("time")
    }

def process_file(input_path):
    if not os.path.exists(input_path):
        print(f"Файл {input_path} не найден")
        return

    try:
        with open(input_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Используем хитрый метод: ищем все JSON объекты в тексте
        # Это сработает, даже если объекты разделены переносами строк
        decoder = json.JSONDecoder()
        pos = 0
        result_lines = []
        
        while pos < len(content):
            # Пропускаем пробелы и переносы перед объектом
            match = re.search(r'\S', content[pos:])
            if not match:
                break
            pos += match.start()
            
            try:
                obj, index = decoder.raw_decode(content[pos:])
                transformed = transform_data(obj)
                if transformed:
                    result_lines.append(json.dumps(transformed, ensure_ascii=False))
                pos += index
            except json.JSONDecodeError:
                pos += 1 # Если встретили мусор, идем дальше

        output_path = "converted_data.json"
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write("\n".join(result_lines))

        print(f"Успех! Обработано {len(result_lines)} валидных точек.")
        print(f"Результат сохранен в: {output_path}")

    except Exception as e:
        print(f"Критическая ошибка: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Использование: python3 script.py 1.json")
    else:
        process_file(sys.argv[1])