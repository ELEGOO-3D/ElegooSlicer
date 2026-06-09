import json
import sys

BOM = b'\xef\xbb\xbf'

file_path = '../resources/profiles/OrcaFilamentLibrary.json'

with open(file_path, 'rb') as fb:
    if fb.read(3) == BOM:
        print(f"Error: {file_path} contains UTF-8 BOM. "
              f"Please re-save the file without BOM.",
              file=sys.stderr)
        sys.exit(1)

with open(file_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

def should_keep(item):
    if item['sub_path'].endswith('@System.json'):
        return item['name'].startswith('Generic')
    return True

data['filament_list'] = [item for item in data['filament_list'] if should_keep(item)]

with open('../resources/profiles/OrcaFilamentLibrary.json', 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=4)