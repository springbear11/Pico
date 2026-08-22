---
name: plugin-writing
description: 编写/修改 PicoATE 仪器插件（示波器/万用表/电源等 VISA 设备），包括抽象接口、桥接层、厂商实现、CMake 注册、注册表部署、站点配置与真机验证序列。Use when creating or extending an instrument plugin under templates/, deploying it into the UI registry, or debugging plugin/hardware integration issues.
---

# PicoATE 仪器插件编写

## 目标

新增一台仪器（示波器/万用表/电源/CAN 卡等）的 PicoATE 插件，使其能在 Station 绑定、Flow 编辑、引擎执行三层正常使用：

```text
抽象接口(ScopeAdapter.h) + 桥接(XxxPluginBridge.cpp) + 厂商实现(VENDOR/XxxAdapter.cpp)
→ PicoATE.<CATEGORY>.<VENDOR>.dll → plugins/PluginRegistry.json → StationSystem.json 绑定 → 验证序列
```

参考模板：`Pico/templates/` 下的 `DMM/`（VISA/SCPI 仪器最佳范本）、`PSU/`、`CAN/`、`SCOPE/`。

VISA 会话代码复制指引：**SCOPE 的 VISA 会话（内嵌在 `RigolScopeAdapter.cpp` 匿名 namespace）已实机调试稳定**（普源 DS1104Z 验证过 drainPending/toValidUtf8/错误策略）；新 VISA 插件需要时，把这段内嵌代码复制到自己的厂商 Adapter.cpp 并改日志前缀（`SCOPE_VISA` → 自己的前缀），**不要建立任何跨插件的共享头文件**（插件间保持独立、解耦，各自演化，改一个不牵连其他）。

## 输入与保护

- 优先收集：仪器 SCPI 编程手册/指令参考、`templates/<CATEGORY>/` 现有实现（对照 DMM 的 DmmAdapter.h / DmmPluginBridge.cpp / 厂商 Adapter 内嵌会话实现）、`PluginRegistry.json`、`StationSystem.json`。
- 涉及部署写盘（`PicoATE/plugins`、UI 构建目录）和实机写入时，先说明改动点，等待授权。
- 抽象接口**不得带具体型号名**（如 Rigol/DS1104）；型号差异只在厂商子目录处理。

## 标准流程

### 1. 抽象接口（<Category>Adapter.h）

- 命名：`IOscilloscopeAdapter`/`IDmmAdapter` 风格，命名空间 `PicoATE::Plugins::<Category>`；工厂 `create<Category>Adapter()` + `pluginDescription()`。
- `Result` 结构体：`success/errorCode/errorMessage/value` + 静态 `passed()/failed()`。
- 模式参数用 `int` 常量（耦合/触发/斜率/带宽等），在头文件顶部注释合法值范围，子类做范围校验。
- 方法只暴露业务语义（`setVoltageDiv(channel, voltsPerDiv)`），不暴露 SCPI 字符串；单位用工程单位。

### 2. 桥接层（<Category>PluginBridge.cpp）

- 导出：`PICOATE_DEFINE_LOG_SINK()` + `PicoATE_Execute` / `PicoATE_Describe` / `PicoATE_GetAbiVersion`（AbiVersion=1）。
- 单例 `g_adapter`（工厂创建）+ `g_mutex`；函数名归一化（去掉 `-`/`_`/空格、小写）后分发。
- open/close/health 之外的函数先检查 `isConnected()`，未连接返回 `ScopeNotConnected` 类错误。
- **日志面向研发**：每个函数统一打 `SCOPE_FUNCTION <fn> outcome= code= message=`；带入参的再打一行入参；measure 打结果值。
- 响应结构：`Plugin::response("Passed", outputs, measurements)` / `Plugin::errorResponse(code, message)`。

### 3. VISA 会话（内嵌在厂商 Adapter.cpp，参考 SCOPE/PSU，插件间零依赖）

- **不建共享头文件**：VISA 会话代码（类型定义、加载 visa64.dll、读写、drainPending、toValidUtf8 等）直接内嵌在 `VENDOR/<Vendor>Adapter.cpp` 的匿名 namespace 里（参考 `SCOPE/RIGOL/RigolScopeAdapter.cpp` 与 `PSU/KORAD/KoradPowerSupplyAdapter.cpp`）。各插件各自持有副本，独立演化，改一个不牵连其他。
- **新插件复制来源**：SCOPE 的 VISA 会话已实机调试稳定（普源 DS1104Z 验证过 drainPending/toValidUtf8/错误策略），需要时复制该文件内的内嵌代码并改日志前缀（`SCOPE_VISA` → 自己的前缀）。
- 动态加载 `visa64.dll`（`LoadLibraryW` + `GetProcAddress` 解析 viOpenDefaultRM/viOpen/viClose/viSetAttribute/viWrite/viRead，viClear/viStatusDesc 可选）。
- 读写用 `\n` 终止符；`commandDelayMs` 每条命令后 sleep；`ioTimeoutMs` 设置 VISA 超时。
- 读循环：`viRead` 直到 `VisaSuccessMaxCount`/短读/超时；**超时后 `drainPending()` 排空迟到字节**，防止下次查询读到错位响应。
- 响应字节先过 `toValidUtf8()`（nlohmann dump 遇非法 UTF-8 会抛异常 → Execute 返回 4 空响应）。
- **错误信息两种已验证策略**：(a) 固定英文 + 数值状态码（SCOPE 用，最简单）；(b) `viStatusDesc` → `localTextToUtf8`（CP_ACP 代码页 → UTF-8，KORAD/DMM 用）——直接返回 viStatusDesc 原样会因 GBK 中文使 JSON dump 抛异常（rc=4 空响应），必须转换。
- **验证错误文本时用 `byte[]` 签名按 UTF-8 解码**：P/Invoke `StringBuilder` 默认按 ANSI 解码 native UTF-8 字节 → 中文显示成「浣嶇疆…」乱码，那是测试桩假象不是插件缺陷（DMM 已踩过，勿误判）。
- 日志：连接/选项/库加载/每个 viXxx 状态码/写命令字节/读次数与字节/原始响应/数值解析结果。

### 4. 厂商实现（VENDOR/<Vendor>Adapter.cpp）

- `pluginDescription()` 返回完整 JSON：schema/picoate.plugin、pluginId/moduleId（`picoate.<cat>.<vendor>` / `plugin.<cat>.<vendor>`）、**category 必须与目录名一致**、connectionKinds、每个函数的 inputs/outputs/timeoutMs/stepKind。
- **枚举输入必须用下拉框**：凡是取值可固定枚举的 input（测量项、通道号、耦合/模式/斜率/扫描等模式参数、布尔开关、串口 parity/stopBits 等），一律 `"type": "enum"` + `"options": [{"label": "显示名", "value": 实际值}]`（label 是 UI 下拉显示，value 是插件收到的值）。UI 的 StepPropertyEditor 对 enum 渲染 QComboBox，**天然禁止绑定变量**——自由文本/整数框允许拖入 `${step:x.outcome}` 这类表达式，会把枚举字段错绑成 "Passed" 等垃圾值（SCOPE measure item 已踩过）。value 必须与实现里的解析值严格一致（如普源测量项 `FREQuency`、Modbus parity `"none"`）。例外：数值输入（电压/电平/档位/地址/超时）保持 number/integer，不要枚举。
- **`Json::array()` 构造 options 必须双层**：用 C++ `Json::array(...)` 时每个 option 要独立成 `{{"label", "x"}, {"value", y}}` 对象，多个用逗号分隔（参考 `CAN/CX/CxCanAdapter.cpp` 的 `deviceType`/`bitrate`）：`Json::array({{"label","A"},{"value",1}}, {{"label","B"},{"value",2}})`。若写成单层 `Json::array({{"label","A"},{"value",1},...})`，每个 `{"label",...}`/`{"value",...}` 会被拆成独立单键对象，UI 扫描报 `options[i]: Expected object`（Modbus RTU 已踩过）。**纯 JSON 字符串（`R"json(...)"` 内）不受影响**，`[{...},{...}]` 直接写即可——只有 `Json::array()` C++ 调用需要双层。
- SCPI 命令映射到接口方法；数值格式化用 `snprintf %g`（SCPI 接受小数/科学计数）；范围校验返回 `Result::failed`。
- 类实现 PIMPL（`Impl` 持 `VisaScpiSession`），工厂 `create<Category>Adapter()` 返回具体实现。

### 5. 构建注册

- `templates/CMakeLists.txt` 加一行 `picoate_add_plugin_category(<CATEGORY> "${CMAKE_SOURCE_DIR}/<CATEGORY>/<Category>PluginBridge.cpp")`（自动发现厂商子目录，输出 `PicoATE.<CATEGORY>.<VENDOR>.dll`）。
- 编译：`cmake --preset vs2022` 后 `cmake --build --preset vs2022-debug --target PicoATE_<CATEGORY>_<VENDOR>`（Debug 和 Release 都要）。
- **编译文件必须纯 ASCII**：中文注释在 GBK 代码页下会吞掉后续声明（C4819/C2447），注释一律英文。

### 5.1 插件目录功能介绍

- 每个新插件必须在自己的 Vendor 目录 `templates/<CATEGORY>/<VENDOR>/README.md` 放一份功能介绍；目录级 README 是交付物，不用等到发布时再补。
- 内容参考 `templates/DMM/README.md`，至少说明：插件型号/用途、DLL 文件名、`driverId`/`moduleId`、通信方式与关键默认参数、已实现函数清单、构建命令、Station/全功能 Sequence 文件位置和必要的安全/实机前置条件。
- README 中的函数清单必须与 `pluginDescription()` 保持一致；新增、删除或重命名公开函数时，同步更新 README 和全功能序列。不要把未实现功能写成“支持”。
- 多型号插件各自维护自己的 README；不要用一个模糊的分类 README 代替 Vendor 目录说明。分类级 README（如 DMM/README.md）可以保留用于总览，但不能替代目录级说明。

### 6. 部署与注册表

- 除非用户另行指定，工具包根目录固定为 `C:\\Work\\PicoATE`。新插件必须同时交付到该工具包：DLL 与实际所需的所有非系统运行时依赖库放入 `C:\\Work\\PicoATE\\plugins\\`；不要只验证构建目录下的 DLL。
- DLL 拷到：`PicoATE/plugins/`、站点本地 `templates/<CATEGORY>/<VENDOR>/plugins/`、以及 **UI 实际读取的每个目录**——UI 按 exe 所在目录读 `plugins/PluginRegistry.json`（portable Release UI、ui/src/Debug、ui/src/Release），不是只改 `PicoATE/plugins`。
- 注册表条目 = `{abiVersion:1, description:<PicoATE_Describe 原始输出>, dll:"PicoATE.X.Y.dll", moduleId}`。
- **注册表拼接用 Describe 原始输出做文本替换**；不要用 PowerShell `ConvertTo-Json` 重序列化深层 description（会截断），也不要手写对象拼接（易错）。
- 校验注册表的 `moduleId`、分类、DLL 文件名和 `connectionKinds` 与 `PicoATE_Describe` 原始输出一致；以已安装的 `C:\\Work\\PicoATE` 运行时完成扫描和调用验证。
- UI 需重启（注册表启动时加载）；改动后如仍显示旧值，用"扫描插件"重建。若已知串口仍无法扫描出来，先对比实际运行的 UI/Core 可执行文件时间戳与源码版本，再判断是程序版本问题还是插件问题；Windows 串口基线可从 `HKLM\\HARDWARE\\DEVICEMAP\\SERIALCOMM` 枚举（本次 CP210x 实机为 `COM5`）。

### 7. 站点与验证序列

- 每个新插件都必须建立同名项目 `C:\\Work\\PicoATE\\projects\\<ProjectName>\\`，至少交付 `StationSystem.json` 和 `<plugin>_full_function_sequence.json`。该项目是工具包交付物，不是仅供源码调试的临时文件。
- 全功能序列必须覆盖 `pluginDescription()` 公开的每个函数；每个写入/输出类函数都要有可观察的回读、测量或状态判定，不能只验证命令发送成功。
- 生命周期保持简单：`Setup` 只放 `open`，`Main` 放 health/read/config/write/readback/功能验证，`Cleanup` 只放 `close`。不要为了使脚本看起来完整而把 Setup 的结果跨阶段引用到 Cleanup；临时状态确需恢复时，在 Main 里执行补偿动作并设计其失败策略。
- `StationSystem.json`：deviceType 用框架既有类型（SCOPE/DMM/PSU/CAN/MODBUS），driverId=`plugin.<cat>.<vendor>`，`ioTimeoutMs>=5000`、`commandDelayMs 50`（2000ms 超时在命令连发后会误报）。
- 验证序列覆盖全部函数：open→identity→reset→clear→各配置→run→测量（带 Limit）→single/forceTrigger→autoScale→query/write→stop→close；测量步骤包 TestItem 重试（如 10 次/1s）。

## 实机调试要点

- **普源示波器**：触发电平必须设在信号幅值范围内（如方波中点 1.5V），0V 贴信号底部会导致测量全部返回 9.9E37（"无有效测量"哨兵值），且会污染后续单次触发。
- `*RST`/`:AUToscale` 后仪器忙，紧跟的查询会超时——序列里加 2s 等待；查询步骤本身加 retry 兜底 USB 偶发超时。
- 单次触发（SINGLE）前确认运行期触发已正常（运行期触发坏了，单帧测量也会跟着 9.9E37）。
- 错误排查先看 `SCOPE_VISA` 字节级日志：写命令→状态码→读字节→原始响应→解析值。
- Modbus RTU 读响应必须校验从站地址、功能码、字节数和 CRC；不能把 Modbus TCP 的 MBAP 规则套到 RTU 日志。设备可能有非标准但稳定的返回尾部：Switch Board 实机对从 `0x0000`、`count=1` 的 FC03 请求返回直至 `0x0008` 的 9 个寄存器。适配层应接受 CRC 正确且不少于请求数量的有效帧，并只把请求的寄存器范围返回给业务层；不要硬性要求响应寄存器数恰好等于 `count`。

## 输出格式

- 先给结论（插件可用 / 需修改），再列：接口设计、函数清单（与 pluginDescription 对齐）、部署位置、验证结果。
- 每个问题给出：位置、证据（日志/Describe）、建议修改。

## 安全边界

- 不动引擎和 UI 源码（src/core、ui/）；只动 `templates/` 插件、`PicoATE/plugins` 部署文件、项目站点/序列。需要动引擎/UI 必须先告知。
- 涉及实机写入前，明确说明会使哪些输出/继电器动作、预计持续多久及恢复方式，并取得用户授权。
- 不把"命令发送成功"当作"仪器执行成功"——需要回读或测量证据。
