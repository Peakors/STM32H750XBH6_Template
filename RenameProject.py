import os
import re
import shutil

def validate_filename(name):
    # Windows文件名不能包含的字符
    invalid_chars = r'<>:"/\|?* '
    if any(char in name for char in invalid_chars):
        raise ValueError(f"文件名不能包含以下字符: {invalid_chars}")
    return name

def main():
    try:
        # 1. 获取用户输入并验证
        user_name = input("请输入项目名称(不能包含空格和特殊字符): ").strip()
        user_name = validate_filename(user_name)
        
        # 2. 找到Software文件夹
        software_dir = "1. Software"
        if not os.path.exists(software_dir) or not os.path.isdir(software_dir):
            raise FileNotFoundError(f"找不到目录: {software_dir}")
        
        # 3. 查找.ioc文件
        ioc_files = [f for f in os.listdir(software_dir) if f.endswith('.ioc')]
        if not ioc_files:
            raise FileNotFoundError(f"在 {software_dir} 中找不到.ioc文件")
        if len(ioc_files) > 1:
            raise Exception(f"在 {software_dir} 中找到多个.ioc文件，请确保只有一个")
        
        old_ioc_name = ioc_files[0]
        old_base_name = old_ioc_name[:-4]  # 去掉.ioc后缀
        
        # 5. 重命名.ioc文件
        new_ioc_name = f"{user_name}.ioc"
        old_ioc_path = os.path.join(software_dir, old_ioc_name)
        new_ioc_path = os.path.join(software_dir, new_ioc_name)
        os.rename(old_ioc_path, new_ioc_path)
        print(f"已重命名文件: {old_ioc_name} -> {new_ioc_name}")
        
        # 6. 修改.ioc文件内容
        with open(new_ioc_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        content = content.replace(old_base_name, user_name)
        
        with open(new_ioc_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"已更新 {new_ioc_name} 文件内容")
        
        # 7. 修改CMakeLists.txt文件内容
        cmake_path = os.path.join(software_dir, "CMakeLists.txt")
        if os.path.exists(cmake_path):
            with open(cmake_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            content = content.replace(old_base_name, user_name)
            
            with open(cmake_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"已更新 CMakeLists.txt 文件内容")
        else:
            print(f"警告: 找不到 {cmake_path} 文件")
        
        # 8. 删除.idea文件夹
        idea_dir = os.path.join(software_dir, ".idea")
        if os.path.exists(idea_dir):
            try:
                shutil.rmtree(idea_dir)
                print(f"已删除文件夹: {idea_dir}")
            except Exception as e:
                print(f"警告: 无法删除 {idea_dir} - {str(e)}")
        else:
            print(f"未找到 .idea 文件夹，无需删除")
        
        print("所有操作已完成!")
    
    except Exception as e:
        print(f"发生错误: {str(e)}")

if __name__ == "__main__":
    main()