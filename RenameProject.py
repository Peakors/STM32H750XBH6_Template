import os
import shutil

def validate_filename(name):
    """验证文件名是否符合Windows命名规则"""
    invalid_chars = r'<>:"/\|?* '
    if any(char in name for char in invalid_chars):
        raise ValueError(f"文件名不能包含以下字符: {invalid_chars}")
    if not name.strip():
        raise ValueError("文件名不能为空")
    return name

def find_first_subdirectory(parent_dir):
    """查找父目录下的第一个子目录"""
    for item in sorted(os.listdir(parent_dir)):
        item_path = os.path.join(parent_dir, item)
        if os.path.isdir(item_path):
            return item_path
    raise FileNotFoundError(f"在 {parent_dir} 中找不到任何子目录")

def find_ioc_file(directory):
    """在目录中查找.ioc文件"""
    for item in os.listdir(directory):
        if item.lower().endswith('.ioc'):
            return item
    raise FileNotFoundError(f"在 {directory} 中找不到.ioc文件")

def replace_in_file(file_path, old_str, new_str):
    """替换文件中的字符串"""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        if old_str not in content:
            print(f"警告: 文件 {os.path.basename(file_path)} 中未找到 '{old_str}'")
            return False
        
        content = content.replace(old_str, new_str)
        
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"已更新 {os.path.basename(file_path)} 文件内容")
        return True
    except UnicodeDecodeError:
        print(f"警告: 无法以UTF-8编码读取 {file_path}，跳过内容替换")
        return False

def main():
    try:
        # 1. 获取用户输入并验证
        user_name = input("请输入项目名称(不能包含空格和特殊字符): ").strip()
        user_name = validate_filename(user_name)
        
        # 2. 找到Software文件夹下的第一个子目录
        software_dir = "2. Software"
        if not os.path.exists(software_dir):
            raise FileNotFoundError(f"找不到目录: {software_dir}")
        
        dir1 = find_first_subdirectory(software_dir)
        print(f"找到目标目录: {dir1}")
        
        # 3. 查找.ioc文件
        ioc_file = find_ioc_file(dir1)
        old_base_name = os.path.splitext(ioc_file)[0]  # 去掉.ioc后缀
        
        # 5. 重命名.ioc文件
        new_ioc_name = f"{user_name}.ioc"
        old_ioc_path = os.path.join(dir1, ioc_file)
        new_ioc_path = os.path.join(dir1, new_ioc_name)
        os.rename(old_ioc_path, new_ioc_path)
        print(f"已重命名文件: {ioc_file} -> {new_ioc_name}")
        
        # 6. 修改.ioc文件内容
        replace_in_file(new_ioc_path, old_base_name, user_name)
        
        # 7. 修改CMakeLists.txt文件内容
        cmake_path = os.path.join(dir1, "CMakeLists.txt")
        if os.path.exists(cmake_path):
            replace_in_file(cmake_path, old_base_name, user_name)
        else:
            print(f"警告: 找不到 {os.path.basename(cmake_path)} 文件")
        
        # 8. 删除.idea文件夹
        idea_dir = os.path.join(dir1, ".idea")
        if os.path.exists(idea_dir):
            try:
                shutil.rmtree(idea_dir)
                print(f"已删除文件夹: {os.path.basename(idea_dir)}")
            except Exception as e:
                print(f"警告: 无法删除 {os.path.basename(idea_dir)} - {str(e)}")
        else:
            print(f"未找到 .idea 文件夹，无需删除")
        
        # 9. 修改dir1文件夹名称
        new_dir_path = os.path.join(software_dir, user_name)
        os.rename(dir1, new_dir_path)
        print(f"已重命名目录: {os.path.basename(dir1)} -> {user_name}")
        
        print("\n所有操作已完成!")
    
    except Exception as e:
        print(f"\n发生错误: {str(e)}")
        print("操作已中止，部分更改可能未完成")

if __name__ == "__main__":
    main()