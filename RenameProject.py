import os

def alter(file,old_str,new_str):
    """
    替换文件中的字符串
    :param file:文件名
    :param old_str:旧字符串
    :param new_str:新字符串
    :return:
    """
    
    file_data = ""
    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            if old_str in line:
                line = line.replace(old_str,new_str)
            file_data += line
    with open(file,"w",encoding="utf-8") as f:
        f.write(file_data)

def delete_files():
    # 删除CubeMX创建的项目文件
    os.remove(".cproject")
    os.remove(".mxproject")
    os.remove(".project")
    
    # 删除CLion项目配置
    os.rmdir(".idea")

def get_project_name():
    # 获取当前文件夹的绝对路径
    current_dir = os.path.abspath('.')
    
    # 获取当前文件夹的名字
    dir_name = os.path.basename(current_dir)
    
    # 获取当前项目名
    return dir_name


def rename_dir(project_name, new_project_name):
    os.chdir("../")
    
    os.rename(project_name, new_project_name)
    



if __name__ == "__main__":
    # 将要修改成的项目名
    new_project_name = "new_pro"
    
#    delete_files()
    
    project_name = get_project_name()
    
    
    
    alter(project_name + ".ioc", project_name, new_project_name)
    
    alter("CMakeLists.txt", project_name, new_project_name)
    
    os.rename(project_name + ".ioc", new_project_name + ".ioc")
    
    rename_dir(project_name, new_project_name)
    
    
    
    
    
    
    
    






















