# ============================================================================
#  Library/CMakeLists.txt
#  用户代码库: 存放不属于 CubeMX 生成范围的自定义模块
#  (如 usb_stdio.c 的 USB 虚拟串口 printf/scanf)
#
#  注意: 本文件由用户维护, CubeMX 不会生成也不会覆盖它。
# ============================================================================

# 收集本目录下所有源文件
# 提示: file(GLOB) 是在"配置阶段"展开的, 新增 .c 文件后需要重新 configure
#       (改 CMakeLists.txt 也会触发, 所以一般不用手动管)
file(GLOB USER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c")

add_library(user_library STATIC ${USER_SOURCES})

# ---------------------------------------------------------------------------
# Inc 目录必须用 PUBLIC:
#   - usb_stdio.c 自己要用
#   - CubeMX 生成的 USB_DEVICE/App/usbd_cdc_if.c 要 include "usb_stdio.h"
#     而那个文件属于主可执行目标, 只有 PUBLIC/INTERFACE 才能传播过去
# ---------------------------------------------------------------------------
target_include_directories(user_library
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)

# ---------------------------------------------------------------------------
# 链接 stm32cubemx, 并且必须是 PUBLIC:
#   - usb_stdio.c 里 include 了 "usb_device.h" / "usbd_cdc_if.h",
#     这些头文件的搜索路径挂在 stm32cubemx (INTERFACE 库) 上
#   - PUBLIC 让它继续传播给主目标, 于是 usbd_cdc_if.c 也能找到 usb_stdio.h
#   - 顺带把 USE_HAL_DRIVER 等宏也带过来了
# 用 PRIVATE 的话: usbd_cdc_if.c 会报 "usb_stdio.h: No such file or directory"
# ---------------------------------------------------------------------------
target_link_libraries(user_library
        PUBLIC
        stm32cubemx
)