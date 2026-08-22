# PicoATE 插件开发检查表

按阶段逐项核对，插件开发/发布前过一遍。

## 1. 源码结构（templates/<CATEGORY>/）

- [ ] `<Category>Adapter.h`：抽象接口，命名空间 `PicoATE::Plugins::<Category>`，无型号名
- [ ] `Result` 结构体 + 模式 int 常量（注释合法范围）
- [ ] 工厂 `create<Category>Adapter()` + `pluginDescription()`
- [ ] `<Category>PluginBridge.cpp`：PicoATE_Execute/Describe/GetAbiVersion + 函数分发 + 统一结果日志
- [ ] `VENDOR/<Vendor>Adapter.h/.cpp`：PIMPL + 内嵌会话 + SCPI 映射 + pluginDescription JSON
- [ ] 内嵌会话：VISA 动态加载、读写、drainPending、toValidUtf8、英文错误
- [ ] `VENDOR/README.md`：目录级功能介绍（型号/用途、DLL、driverId/moduleId、通信参数、已实现函数、构建/Station/Sequence、安全前置）
- [ ] 所有编译文件纯 ASCII（无中文注释）

## 2. pluginDescription JSON

- [ ] `schema: picoate.plugin` / `schemaVersion: 1`
- [ ] `pluginId: picoate.<cat>.<vendor>`、`moduleId: plugin.<cat>.<vendor>`
- [ ] `category` 与目录名一致（如 SCOPE）
- [ ] `connectionKinds`（如 visa）
- [ ] 每个函数：id/name/description/inputs(key/type/default/min/max/unit)/outputs/timeoutMs/stepKind(close 用 cleanup)
- [ ] open 的 inputs 含 address/visaLibrary/ioTimeoutMs/commandDelayMs/writeTermination/readTermination

## 3. 编译

- [ ] `templates/CMakeLists.txt` 已加 `picoate_add_plugin_category(<CATEGORY> ...)`
- [ ] `cmake --preset vs2022` 配置无 error
- [ ] Debug：`cmake --build --preset vs2022-debug --target PicoATE_<CATEGORY>_<VENDOR>`
- [ ] Release：`cmake --build --preset vs2022-release --target PicoATE_<CATEGORY>_<VENDOR>`

## 4. 导出自检（P/Invoke 或 CLI）

- [ ] `PicoATE_GetAbiVersion() == 1`
- [ ] `PicoATE_Describe` 返回合法 JSON（moduleId/函数数正确）
- [ ] `PicoATE_Execute` open（空地址/坏库名返回干净错误，不 rc=4）
- [ ] 未连接时调用函数返回 `NotConnected` 类错误

## 5. 部署（5 处 DLL + 注册表）

- [ ] `C:\Work\PicoATE\plugins\`（主部署）
- [ ] `templates\<CATEGORY>\<VENDOR>\plugins\`（站点本地）
- [ ] `out\build\vs2022-qt6-all\portable\Release\PicoATE.UI\plugins\`
- [ ] `out\build\vs2022-qt6-all\ui\src\Debug\plugins\`
- [ ] `out\build\vs2022-qt6-all\ui\src\Release\plugins\`
- [ ] 各目录 `PluginRegistry.json` 含新条目（用 Describe 原始输出文本替换）
- [ ] 全局搜无旧 category/旧 moduleId 残留
- [ ] UI 重启后能看到新驱动（否则用"扫描插件"）

## 6. 站点

- [ ] `StationSystem.json`：deviceId/deviceType/driverId=`plugin.<cat>.<vendor>`/resource 正确
- [ ] `ioTimeoutMs >= 5000`、`commandDelayMs 50`
- [ ] pluginRegistry 路径可解析

## 7. 验证序列

- [ ] setup: open；cleanup: close
- [ ] 覆盖全部函数（identity/reset/clear/各配置/run/stop/single/forceTrigger/measure/autoScale/query/write）
- [ ] 测量步骤包 TestItem 重试（10 次/1s）
- [ ] 触发电平设在信号幅值范围内（普源方波用 1.5V，不用 0V）
- [ ] reset/run/autoScale 后加 2s 等待；查询步骤加 retry
- [ ] Limit 判定的实际值引用正确（`${step:<id>.01.outputs.value}`）
- [ ] CLI 实机连跑 3 次全 PASS
- [ ] Vendor README 的函数清单与 `pluginDescription()`、全功能序列三方一致

## 8. 常见坑（已踩过）

| 现象 | 原因 | 处理 |
|---|---|---|
| rc=4 空响应 | 错误文本含 GBK 中文（viStatusDesc）→ JSON dump 抛异常 | 错误固定英文 + 数值状态码；或 viStatusDesc → localTextToUtf8(CP_ACP→UTF-8)（KORAD/DMM 已验证） |
| errorMessage 显示「浣嶇疆…」乱码 | P/Invoke StringBuilder 按 ANSI 解码 UTF-8 字节（测试桩假象） | 改用 byte[] 签名按 UTF-8 解码验证；插件输出本身正确 |
| 下次查询读到旧值 | 读超时后迟到响应留在 VISA 缓冲 | 超时后 drainPending 排空 |
| C2447/C4819 类声明被吞 | 编译文件含中文注释（GBK 代码页） | 注释转英文/纯 ASCII |
| 测量全 9.9E37 | 触发电平 0V 贴信号底部（普源） | 触发电平设波形中点 |
| UI 找不到驱动 | UI 读 exe 目录的 plugins/PluginRegistry.json，不是主目录 | 更新全部 UI 构建目录 |
| Flow 显示旧分类 | pluginDescription 的 category 字段没改 | category 与目录一致 |
| ConvertTo-Json 截断 description | PowerShell 重序列化深层嵌套丢失字段 | 用 Describe 原始输出文本替换 |
| 命令连发后查询超时 | ioTimeoutMs 2000 太短 | 设 5000 + commandDelayMs 50 + 查询 retry |
| measure 发 `:MEASure:ITEM? Passed` 超时 | item 字段被绑定成 `${step:x.outcome}`（字符串框允许拖变量） | item 等枚举字段改 `type: enum` 下拉框（天然禁绑变量） |
| UI 扫描报 `options[i]: Expected object` | `Json::array()` 单层写 options，每个 option 被拆成单键对象 | options 必须双层 `Json::array({{"label","A"},{"value",1}}, ...)`；R"json" 纯文本不受影响 |
