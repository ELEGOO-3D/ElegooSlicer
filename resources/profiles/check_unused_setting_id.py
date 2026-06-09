import os
import json
import sys

BOM = b'\xef\xbb\xbf'


def _check_bom(file_path):
    """Check if file has UTF-8 BOM. If so, print error and return True."""
    with open(file_path, 'rb') as f:
        if f.read(3) == BOM:
            print(f"Error: {file_path} contains UTF-8 BOM. "
                  f"Please re-save the file without BOM.",
                  file=sys.stderr)
            return True
    return False


setting_id_used=set()
setting_id_all=set()
root_dir=os.path.dirname(os.path.abspath(__file__))


def loadBlackList():
    if _check_bom(root_dir+'/blacklist.json'):
        sys.exit(1)
    with open(root_dir+'/blacklist.json', encoding='utf-8') as file:
        data=json.load(file)

    for key,val in data.items():
        for item in val:
            setting_id_used.add(item)
            setting_id_all.add(item)

def traverse_files(path):
    for file in os.listdir(path):
        file_path = os.path.join(path, file)
        if os.path.isdir(file_path):
            traverse_files(file_path)  # 递归遍历子文件夹
        elif file_path.endswith('.json'):
            # 解析 JSON 文件并提取 setting_id 的值
            if _check_bom(file_path):
                continue
            with open(file_path, encoding='utf-8') as f:
                data = json.load(f)
                if 'setting_id' in data:
                    setting_id_all.add(data['setting_id'])

def getUsedId(brand):
    brand_json = root_dir+'/'+brand+'.json'
    if _check_bom(brand_json):
        sys.exit(1)
    with open(brand_json, encoding='utf-8')as file:
        data=json.load(file)

    key_list=["machine_model_list","machine_list","filament_list","process_list"]

    for key in key_list:
          for elem in data[key]:
            path=elem['sub_path']
            sub_path = root_dir+'/'+brand+'/'+path
            if _check_bom(sub_path):
                continue
            with open(sub_path, encoding='utf-8') as file:
                file_data=json.load(file)
            if 'setting_id' in file_data:
                setting_id_used.add(file_data['setting_id'])


def getTotalId(brand):
    traverse_files(root_dir+'/'+brand)


loadBlackList()
getUsedId('BBL')
getTotalId('BBL')

print("unused setting_id :")
print(setting_id_all.difference(setting_id_used))
