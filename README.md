# STM32H750XBH6项目工程模板

基于反客STM32H750XBH6核心板(FK750M5-XBH6)的CLion + CubeMX + CMake + OpenOCD的外部Flash下载算法模板

**若使用当前模板的外部flash下载算法，需要与原理图中QSPI引脚完全相同，否则无法使用。**

联系方式：Peakors@163.com

工程版本：

- Firmware Package Name and Version: STM32Cube FW_H7 V1.12.1
- STM32CubeMX Version: 6.15.0
- STM32CubeCLT Version：1.19.0
- OpenOCD Version：20250710
- CLion 2025.1

实现功能：

- 添加printf和scanf重定向到串口1，具体代码参考`retarget.c`和`retarget.h`.
- 添加USB虚拟串口，感谢群友`琴梨project`
- 添加项目名称修改Python脚本，详见`RenameProject.py`

硬件原理图：

- 见Hardware目录下

感谢：

- https://haobogu.github.io/posts/keyboard/openocd-ospi-flash
- https://github.com/haoruanwn/STM32H750XBH6_CMake_Template
- https://zhuanlan.zhihu.com/p/145801160
- https://github.com/WangHunZi/EmbeddedProjectTemplates

**pyOCD**版本：https://github.com/haoruanwn/STM32H750XBH6_CMake_Template



开发板购买店铺：https://shop212360197.taobao.com

本项目测试板子：https://item.taobao.com/item.htm?id=682521953131









