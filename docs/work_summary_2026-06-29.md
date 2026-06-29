# PicoATE 工作总结（2026-06-29）

## 今日目标

完成 UI-3 实时运行监控，让 Runner 在测试进行中看到 UUT、Step、Attempt、Loop、
Barrier、Cleanup 和设备状态，同时继续保持 UI、任务引擎、业务测试逻辑三层解耦。

## 已完成

| 项目 | 结果 |
|------|------|
| Runtime Event DTO | 新增只读值类型 `RuntimeEvent`，覆盖 Session/UUT/Node/Attempt/Device 等事件 |
| Core 事件出口 | 新增 `IRuntimeEventSink`，Core 不依赖 Qt Widgets，也不持有 UI 对象 |
| 事件顺序 | `RuntimeEventEmitter` 为单次 Session 分配单调递增序号和 UTC 时间 |
| 调度接入 | 接入 Node 开始/结束、Retry、Loop、Barrier、Stop、Cleanup 和 UUT 完成事件 |
| 设备接入 | 接入 configured/connecting/connected/reused/disconnected/error 状态 |
| 线程桥 | Worker 线程写 `BufferedRuntimeEventSink`，主线程不读取可变 Session 对象 |
| 合并限频 | ViewModel 每 50ms 批量刷新一次，缓冲区最多保留 20000 条事件 |
| 实时模型 | `UutStepModel` 增量更新 Step/Attempt/Measurement，新增 `DeviceStatusModel` |
| Runner 页面 | 新增 Devices 页，运行中实时显示逻辑设备、类型、Driver、状态和消息 |
| 最终对账 | 运行结束后使用 `ExecutionReport` 整体覆盖实时状态，报告是最终事实来源 |

## 关键边界

1. Runtime Event 是引擎运行过程事件，不是业务 DLL 内部实时日志。
2. 本次没有修改 `PicoATE_Execute` ABI，也没有要求项目 DLL 增加日志回调。
3. Host stderr、协议错误和进程诊断仍是独立增强项。
4. Event 不携带 `ExecutionSession*`、`UutExecution*`、`NodeActivation*` 或设备 session 指针。
5. UI 事件只负责过程展示；测试最终结果只认 `ExecutionReport`。

## 验证结果

| 验证 | 结果 |
|------|------|
| 完整 Qt UI 编译 | Passed |
| PicoATECoreTests | Passed |
| PicoATEUiTests（12 个测试函数） | Passed |
| Engine + UI 联合 CTest | 13/13 passed |
| 双 UUT Barrier 事件 | Passed |
| Loop 三轮事件 | Passed |
| Retry Attempt 顺序 | Passed |
| Stop + Cleanup 事件 | Passed |
| 实时模型 + 最终报告覆盖 | Passed |
| Device 状态模型 | Passed |

## 当前 UI 能力

- 选择并编译 Sequence/Station JSON；
- 配置 1-64 个 UUT；
- Worker 线程异步运行，主线程安全 Stop；
- 运行中实时查看 UUT、Step、Attempt、Measurement 和 Loop iteration；
- 查看 Barrier 等待/释放、资源等待、Cleanup 和设备连接状态；
- 运行结束后查看完整只读 ExecutionReport。

## 下一步

### UI-4 报告与历史（已完成）

| 项目 | 结果 |
|------|------|
| ExecutionReport JSON | 新增 V1 schema，完整保存 UUT/Step/Attempt/Loop/Measurement |
| 兼容策略 | 缺少可选字段可加载，未知未来版本明确拒绝 |
| 自动历史 | 最终报告自动保存到 AppLocalDataLocation/history |
| 索引恢复 | index.json 原子写入，损坏后扫描有效报告自动重建 |
| History 页 | 支持文本筛选、双击加载历史结果 |
| JSON/CSV 导出 | JSON 保持原格式；CSV 支持中文、Limit、Loop 和错误字段 |
| UI 测试 | 增至 15 个测试函数，联合 CTest 仍为 13/13 passed |

## 下一步

进入 UI-5 Sequence Editor。先建立能保留未知扩展字段的 `SequenceDocument`，再实现
Group/Step 树和属性编辑；Host/Transport 诊断仍保持独立任务。

### 真实 CAN USB 验证准备（追加）

- 新增 `templates/can_hardware_dll/` 独立 C++ DLL 模板；
- 支持 open/status/write/read/requestResponse/close；
- 使用 `persistent-qprocess` 保持硬件句柄跨 Step 存活；
- NativeHost 内 DLL 保持加载到进程退出；
- 软件回环 Sequence 已实际运行，四个步骤全部 Passed；
- 真实接入只需在 `VendorCanAdapter.cpp` 映射厂商 SDK。
