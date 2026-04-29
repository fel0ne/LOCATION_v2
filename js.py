import json
import os
import sys

def transform_friend_data(item):
    """
    Трансформирует лог друга, сохраняя оригинального провайдера
    """
    loc = item.get("location", {})
    
    # Берем провайдера из данных друга, если его нет — ставим "network" для совместимости
    friend_provider = item.get("provider", loc.get("provider", "network"))
    
    tele_list = item.get("telephony", [])
    primary_cell = tele_list[0] if tele_list else {}
    
    ident = primary_cell.get("CellIdentityLte", {})
    sig = primary_cell.get("CellSignalStrengthLte", {})

    rssnr = sig.get("RSSNR", 0)
    if rssnr == 2147483647: rssnr = 0

    return {
        "accuracy": loc.get("accuracy", 100.0),
        "latitude": loc.get("latitude", 0.0),
        "longitude": loc.get("longitude", 0.0),
        "provider": friend_provider,  # Теперь здесь будет то, что в исходнике
        "recordedTime": loc.get("time", item.get("timestamp")),
        "source": "friend_log",
        "telephony": {
            "LTE": {
                "identity": {
                    "band": int(ident.get("Band", 0)),
                    "ci": int(ident.get("CellIdentity", 0)),
                    "earfcn": int(ident.get("EARFCN", 0)),
                    "mcc": str(ident.get("MCC", "250")),
                    "mnc": str(ident.get("MNC", "02")), # У друга часто MNC 02 (Мегафон)
                    "pci": int(ident.get("PCI", 0)),
                    "tac": int(ident.get("TAC", 0))
                },
                "signal": {
                    "asu": int(sig.get("ASU_Level", 0)),
                    "rsrp": int(sig.get("RSRP", -110)),
                    "rsrq": int(sig.get("RSRQ", -15)),
                    "rssnr": int(rssnr),
                    "ta": int(sig.get("Timing_Advance", 0)) if sig.get("Timing_Advance") != 2147483647 else 0
                }
            }
        },
        "timestamp": item.get("timestamp")
    }

def process_file(input_path):
    if not os.path.exists(input_path):
        print(f"Файл {input_path} не найден")
        return

    result_lines = []
    try:
        with open(input_path, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line: continue
                try:
                    # У друга в конце может быть обрезанный JSON, обрабатываем это
                    obj = json.loads(line)
                    transformed = transform_friend_data(obj)
                    result_lines.append(json.dumps(transformed, ensure_ascii=False))
                except json.JSONDecodeError:
                    print(f"Пропущена битая строка {line_num}")

        output_path = "converted_friend_data.json"
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write("\n".join(result_lines))

        print(f"Готово! Обработано {len(result_lines)} строк.")
        print(f"Результат в файле: {output_path}")

    except Exception as e:
        print(f"Ошибка: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Использование: python3 script.py friend_file.json")
    else:
        process_file(sys.argv[1])