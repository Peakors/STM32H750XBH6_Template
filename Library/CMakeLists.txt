# 查找目录下的所有源文件
file(GLOB SOURCES "Src/*.c")

# 添加一个静态库
add_library(user_library STATIC ${SOURCES})

# 指定头文件目录
target_include_directories(user_library
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Inc"

)

# 链接 STM32CubeMX 生成的目标
target_link_libraries(user_library
        PRIVATE
        stm32cubemx
)