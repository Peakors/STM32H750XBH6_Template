# BSP（板级支持包）：W25Qxx(QSPI Flash) 驱动 + LED 语义层
#   注意：LED 的引脚配置由 CubeMX 生成的 Core/Src/gpio.c 负责，
#         BSP 里只有 BSP/Inc/led.h 这个纯宏头文件，没有 led.c

# 源文件
set(BSP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/qspi_w25q64.c"
)

# 添加一个静态库
add_library(bsp STATIC ${BSP_SOURCES})

# 指定头文件目录
target_include_directories(bsp
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)

# 链接 STM32CubeMX 生成的目标（提供 HAL 头文件路径、Core/Inc 以及编译宏）
target_link_libraries(bsp
        PUBLIC
        stm32cubemx
)
