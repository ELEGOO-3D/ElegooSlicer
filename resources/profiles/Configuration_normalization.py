import json
import os

def format_json_custom(file_path, extra_indent_keys):
    with open(file_path, 'r', encoding='utf-8') as file:
        data = json.load(file)

    # 将 JSON 数据格式化为字符串，4 个空格的缩进
    json_str = json.dumps(data, indent=4, ensure_ascii=False)

    # 移除最外层大括号的缩进
    json_str = json_str.strip()
    if json_str.startswith("{"):
        json_str = "{" + json_str[1:]
    if json_str.endswith("}"):
        json_str = json_str[:-1] + "}"

    # 对指定字段增加额外的4个空格缩进，形成总共8个空格的效果
    for key in extra_indent_keys:
        json_str = json_str.replace(f'"{key}": {{', f'        "{key}": {{')  # 对特定键额外增加4个空格

    with open(file_path, 'w', encoding='utf-8') as file:
        file.write(json_str)

def format_json_files_in_folder(folder_path, extra_indent_keys):
    for root, _, files in os.walk(folder_path):
        for filename in files:
            if filename.endswith('.json'):
                file_path = os.path.join(root, filename)
                format_json_custom(file_path, extra_indent_keys)
                print(f"Formatted {file_path}")

# 使用方式：指定包含 JSON 文件的文件夹路径，并提供需要额外缩进的字段名称
folder_path = "/Users/macbookpro/Projects/ElegooSlicer/resources/profiles/test"  # 替换为你的文件夹路径
extra_indent_keys = ["nested_key"]  # 替换为你需要额外缩进的字段
format_json_files_in_folder(folder_path, extra_indent_keys)
