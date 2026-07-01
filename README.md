# PicoATE

PicoATE 是面向 ATE 产线测试的 C++20/Qt6 执行框架，目标是让 UI、任务调度和业务测试逻辑保持三层解耦。交付新项目时，测试流程主要通过 JSON 配置，设备通信和协议解析通过业务模块扩展，不修改 UI 与调度内核。

## 当前能力

- JSON Sequence 编译为不可变 `ExecutionPlan`
- Setup/Main/Custom/Cleanup、Retry、Stop、Barrier、For Loop、嵌套 TestItem
- 多 UUT、资源仲裁、设备 Session、NativeHost DLL 隔离
- 局部 Step ID/Key 与作用域节点路径，例如 `001.rx`
- 每个 UUT 独立的 `ExecutionResultStore`
- `${step:001.rx.outputs.frame}` 跨测试项结果引用
- `${step:rx.outputs.frame}` 当前 TestItem 相对引用
- 内置 Limit 节点支持数值区间/单边阈值、容差、字符串和布尔比较
- RuntimeEvent 实时事件、ExecutionReport、JSON/CSV 报告
- Qt Widgets Runner 与实时增量模型
- CLI 逐 Step 实时输出及便携 Release 目录

PicoATE 内置 C/C++ DLL 和 Python 脚本加载能力。其他语言由项目团队自行打包为 `.exe`，框架不提供对应 SDK 或模板。

## VS2022 与 Qt6

Qt 默认路径：

```text
D:/QT/6.9.1/msvc2022_64
```

仅引擎：

```powershell
cmake --preset vs2022-qt6
cmake --build --preset vs2022-qt6-debug
ctest --preset vs2022-qt6-debug
```

引擎与 UI：

```powershell
cmake --preset vs2022-qt6-all
cmake --build --preset vs2022-qt6-all-debug
ctest --preset vs2022-qt6-all-debug
```

对应解决方案：

```text
out/build/vs2022-qt6/PicoATE.sln
out/build/vs2022-qt6-all/PicoATE.All.sln
ui/out/build/vs2022-qt6/PicoATE.UI.sln
```

## CLI

开发目录运行：

```powershell
./out/build/vs2022-qt6-all/src/cli/Debug/PicoATE.Cli.exe run examples/scoped_result_sequence.json --uuts 2
```

生成可复制到其他电脑的 Release 目录：

```powershell
cmake --build --preset vs2022-qt6-cli-portable-release
```

输出位置：

```text
out/build/vs2022-qt6-all/portable/Release/PicoATE.Cli/
```

该目录包含 CLI、Qt6Core、VC143 x64 Runtime、NativeHost、Fake/Mock Host、测试 DLL 和 examples，可整体复制使用。

## 文档

从 [文档索引](docs/文档索引.md) 开始阅读。当前进度和每日改动统一记录在 [开发日志](docs/开发日志.md) 与 [开发进度与计划](docs/开发进度与计划.md)。
