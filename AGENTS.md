# AGENTS.md — 项目协作规范

本文件是仓库的协作与交付规范，适用于所有参与者（包括 AI 编码代理）。

## 1. 项目概述

基于反客 STM32H750XBH6 核心板（FK750M5-XBH6 + W25Q64）的 Bootloader + APP 模板，工具链为 CLion + STM32CubeMX + CMake + arm-none-eabi-GCC + OpenOCD。

| 目录 | 内容 |
|---|---|
| `1.Docs/` | 项目文档（Markdown），配图放 `1.Docs/Pictures/` |
| `2.Software/STM32H750XBH6_Bootloader/` | Bootloader，链接到内部 Flash `0x08000000`（128KB） |
| `2.Software/STM32H750XBH6_APP/` | APP，链接到外部 QSPI Flash `0x90000000`（XIP） |
| `3.Hardware/` | 原理图与数据手册（PDF，仅查阅，不修改） |
| `4.Release/` | 发布目录：预编译固件 hex + OpenOCD 烧写算法（有意入库，.gitignore 显式例外） |

## 2. 构建与验证

- 依赖 STM32CubeCLT（arm-none-eabi-gcc、CMake、Ninja）已在 PATH 中；
- 构建命令（两个工程相同）：

  ```bash
  cd 2.Software/<工程名>
  cmake --preset Debug        # 或 Release
  cmake --build --preset Debug
  ```

- 产物位于 `build/<preset>/`（`build/` 已被 .gitignore 忽略，严禁入库）；
- **固件发布流程**：固件或烧写算法更新后，须将 Release 构建的 hex 同步到 `4.Release/Bootloader|APP/`、算法副本同步到 `4.Release/OpenOCD/`，并在同一提交中说明；`4.Release/` 是 .gitignore 的显式例外，仓库其余位置的构建产物仍禁止入库；
- **任何代码改动交付前，两个工程的 Debug 与 Release 构建必须全部通过**；
- 涉及链接脚本、启动文件、向量表的改动，还须用 `arm-none-eabi-objdump -h` / `readelf` 核对 `.isr_vector` 段地址、初始 MSP 与入口地址是否符合预期。

## 3. 代码修改规则

1. `Core/` 下 CubeMX 生成的文件，只能修改 `/* USER CODE BEGIN xxx */ ... /* USER CODE END xxx */` 区段，其余部分会被 CubeMX 重新生成时覆盖；
2. `Drivers/`、`Middlewares/` 为 ST 官方库，**禁止修改**；
3. 分层原则：外设寄存器配置归 CubeMX（改 `.ioc` 后重新生成）；Flash 等芯片命令协议、板级逻辑写在 `BSP/`；可复用功能（如 printf 重定向）写在 `Library/`；
4. 链接基址不可混淆：**Bootloader = `0x08000000`（内部 Flash），APP = `0x90000000`（外部 QSPI）**，改动任一工程链接脚本时同步检查 `SystemInit()` 中 `SCB->VTOR` 是否一致；
5. Windows 文件系统大小写不敏感，但 CI（Linux）敏感：新增或重命名文件时，文件名大小写必须与代码/CMake 中的引用完全一致（统一小写，如 `STM32H750xx_FLASH.ld`）。

## 4. Git 提交规范

1. 每完成一处**独立**改动，必须立即创建一个对应的 Git Commit，以便追踪与回滚；一次提交只做一件事，禁止混入无关改动；
2. 提交信息格式：`<类型>(<范围>): <中文描述>`，正文说明改动动机与验证结果；
   - 类型：`feat` / `fix` / `docs` / `build` / `refactor` / `test` / `ci` / `chore`
   - 范围示例：`bootloader` / `app` / `repo` / `docs`
3. 换行符统一 LF（已由 `.gitattributes` 约定），不要引入 CRLF 或混合行尾；
4. 不改写已推送到远端的历史提交。

## 5. 文档规范

1. 所有输出文档统一放到 `1.Docs/` 文件夹下，使用 Markdown 格式编写，文档图片放 `1.Docs/Pictures/`；
2. 修改了行为、接口或配置后，须同步更新 `README.md` 与相关文档；
3. 文档中引用工程代码时，给出 `文件路径:行号` 并附代码内容，便于跨版本定位。

## 6. 测试与交付

1. 每次改动后，都必须编写或更新相关测试/验证内容（本仓库以「构建通过 + 产物布局核对」为基线验证，真机验证项在文档中注明）；
2. 交付前确保所有测试和验证全部通过，并在总结中给出验证结果；未验证的内容必须明确标注。
