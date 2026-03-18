import os
import json
import re
import sys
from pathlib import Path

def update_version_in_json(folder_path, new_version):
    for file_name in os.listdir(folder_path):
        file_path = os.path.join(folder_path, file_name)
        if os.path.isfile(file_path) and file_name.endswith('.json'):
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                if 'version' in data:
                    data['version'] = new_version
                    with open(file_path, 'w', encoding='utf-8') as f:
                        json.dump(data, f, ensure_ascii=False, indent=4)
                    print(f"Updated version in {file_path}")
                else:
                    print(f"No 'version' key found in {file_path}")
            
            except Exception as e:
                print(f"Failed to process {file_path}: {e}")


def normalize_version(version_text):
    """Normalize version to XX.XX.XX.XX (e.g. 1.3.2.0 -> 01.03.02.00)."""
    if version_text is None:
        return None

    candidate = version_text.strip()
    if not candidate:
        return None

    parts = candidate.split('.')
    if len(parts) != 4:
        return None

    normalized_parts = []
    for part in parts:
        if not part.isdigit():
            return None
        value = int(part)
        if value < 0 or value > 99:
            return None
        normalized_parts.append(f"{value:02d}")

    return '.'.join(normalized_parts)


scripts_dir = Path(__file__).resolve().parent
root_dir = scripts_dir.parent
profiles_dir = root_dir / 'resources' / 'profiles'
version_pattern = r'^\d{2}\.\d{2}\.\d{2}\.\d{2}$'


arg_version = sys.argv[1] if len(sys.argv) > 1 else None
new_version = normalize_version(arg_version)

if arg_version and not new_version:
    print("Invalid version argument. Please use 4 numeric parts between 0-99, for example: 1.3.2.0 or 01.03.02.00")
    sys.exit(1)

while not new_version:
    input_version = input("Please enter the new version (format: 01.01.05.00 or 1.1.5.0): ")
    new_version = normalize_version(input_version)
    if not new_version:
        print("Invalid version format. Please follow the format: X.X.X.X or XX.XX.XX.XX, where each part is 0-99.")

if not re.match(version_pattern, new_version):
    print("Unexpected error: normalized version is not in XX.XX.XX.XX format.")
    sys.exit(1)


update_version_in_json(profiles_dir, new_version)
