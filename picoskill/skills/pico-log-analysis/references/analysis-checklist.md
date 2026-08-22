# PicoATE 日志分析检查表

## 证据采集

- [ ] 确认日志、CSV、sequence、寄存器表版本和时间戳
- [ ] 确认是否存在更早/更晚版本日志
- [ ] 确认测试项、步骤 ID、TestItem 层级和重试次数

## Modbus 帧

- [ ] TX/RX transaction ID 一致
- [ ] Unit ID 正确
- [ ] 功能码正确
- [ ] 地址和 count 正确
- [ ] 响应字节数等于 `count × 2`
- [ ] FC10 回包后有必要的 FC03 回读

## 解析

- [ ] registerOffset 是返回寄存器数组索引
- [ ] decodeRegisters 的 dataType/layout 正确
- [ ] rawHex 与选中的寄存器一致
- [ ] decodeBinary 的 offset 从 0 开始
- [ ] unit 是 byte 还是 bit
- [ ] lsb0/msb0 和 byteOrder 正确
- [ ] scale/valueOffset 与协议表一致
- [ ] source 引用属于当前执行分支

## 判定

- [ ] 先看 TestItem 的 `_TESTITEM_END` 与整体 `RESULT`，再下结论
- [ ] 带 retry 的监控步骤确认了最后一次 attempt 的结果，而不是只看中间的 FAIL
- [ ] 检索失败证据时同时检索了 PASS 证据，避免漏掉重试后通过的部分
- [ ] 人工确认步骤确认了操作员动作真的发生（寄存器状态在重试期间发生变化）
- [ ] Limit 实际值和期望值已记录
- [ ] 设置动作确实产生硬件刺激
- [ ] 刺激前后原始帧发生符合预期的变化
- [ ] 恢复动作执行并回读确认
- [ ] 人工 PASS 没有被当作寄存器 PASS
- [ ] 未定义位或固定默认值没有被误判为有效 PASS

## 生命周期

- [ ] Main 失败后普通节点被跳过
- [ ] alwaysRun 补偿动作实际执行
- [ ] Main 能完成
- [ ] cleanup 被激活并执行
- [ ] cleanup 结果进入最终报告
