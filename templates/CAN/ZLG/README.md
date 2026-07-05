# ZLG 插件预留目录

在这里新增 `ZlgCanAdapter.h/.cpp`，实现 `CAN/CanAdapter.h` 中的 `ICanAdapter`，并在
`.cpp` 中提供：

```cpp
std::unique_ptr<ICanAdapter> createCanAdapter();
```

重新运行根目录 `生成解决方案.cmd` 后，会自动出现并生成
`PicoATE.CAN.ZLG.dll`，不需要修改顶层 CMake。
