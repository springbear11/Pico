# SCOPE 插件功能详解（Rigol DS1000Z 系列）

本文档逐个介绍 `PicoATE.SCOPE.RIGOL.dll` 的 28 个功能：**它是干什么的、发什么 SCPI 命令、影响哪些测量、什么时候用、有哪些坑**。

标题采用 **UI 显示的插件功能名（函数 id）** 格式，与 Step 属性面板/流程编辑里看到的完全一致。面向研发与测试序列编写者。命令行为以普源 DS1000Z 系列（DS1104Z Plus / DS1074Z / DS1054Z）实测为准。

---

## 一、生命周期

### 1. Open Oscilloscope（open）— 打开示波器（初始化）

- **作用**：打开 VISA 连接，加载 `visa64.dll`，设置 I/O 超时、命令间隔、读写终止符。
- **SCPI**：无（VISA 层 viOpen）。
- **输入**：address（VISA 资源）、visaLibrary、ioTimeoutMs、commandDelayMs、writeTermination、readTermination。
- **要点**：`ioTimeoutMs` 建议 ≥5000（2000 在命令连发后会误报超时）；`commandDelayMs` 建议 50。
- **坑**：地址为空返回 `VisaAddressRequired`；仪器未接/拔线返回 `VisaInstrumentOpenFailed`（0xBFFF0011 = VI_ERROR_RSRC_NFOUND，不是插件 bug）。

### 2. Close Oscilloscope（close）— 关闭示波器（释放）

- **作用**：关闭 VISA 会话、释放资源管理器。
- **SCPI**：无（VISA 层 viClose）。
- **要点**：应放在序列 cleanup 组，确保仪器和资源被释放。

---

## 二、IEEE488.2 基础

### 3. Read Identity（identity）— 读取仪器身份

- **作用**：查询仪器型号、序列号、固件版本。
- **SCPI**：`*IDN?`
- **实测**：`RIGOL TECHNOLOGIES,DS1104Z Plus,DS1ZC260800273,00.04.05.SP2`
- **用途**：序列开头校验型号正确（Limit contains `DS1104`）。

### 4. Reset Oscilloscope（reset）— 示波器复位（恢复出厂）

- **作用**：把仪器**所有设置恢复出厂默认**（IEEE 488.2 标准行为）。
- **SCPI**：`*RST`
- **⚠️ 关键坑**：
  - **不是"清状态"，是恢复出厂**——触发电平回 0V、档位回默认、探头回 1X、只开 CH1。接 CAL 方波会看到波形"变了"。
  - **复位期间仪器忙**（约 1~2s），紧跟的查询会 VISA 超时。序列里 reset 后必须加 wait（建议 2~5s）。
  - **复位后必须重新配置**：触发电平（CAL 方波设 1.5V）、垂直档位、时基、探头衰减——否则测量可能全 9.9E37（触发 0V 贴信号底部）。
- **用法**：序列开头做"干净起点"用，之后按测试需求重新配置。

### 5. Clear Oscilloscope Status（clear）— 清除状态

- **作用**：清除仪器错误队列/状态寄存器。
- **SCPI**：`*CLS`（**不是 viClear**）。
- **⚠️ 已修复的坑**：旧版用 viClear（VISA 设备清除），在 USBTMC 上会 abort 未完成传输、重置 bulk 端点，**导致紧随其后的第一个查询超时**（实测 `*IDN?` 超时）。现在固定用 `*CLS`，对后续查询零副作用。

### 6. Lock Front Panel（lockFrontPanel）— 锁定/解锁面板

- **作用**：锁定/解锁示波器前面板按键（防止测试时误操作）。
- **SCPI**：`:SYSTem:LOCKed ON/OFF`
- **输入**：lock（0=解锁，1=锁定，下拉框）。

---

## 三、运行控制

### 7. Run Acquisition（run）— 开始采集（运行）

- **作用**：让示波器进入连续采集运行状态。
- **SCPI**：`:RUN`
- **要点**：AUTO 触发下波形持续滚动；NORM 触发下不满足条件就不刷新。

### 8. Stop Acquisition（stop）— 停止采集

- **作用**：停止采集，屏幕定格在当前帧。
- **SCPI**：`:STOP`
- **用途**：采到一帧后定格观察/测量；或采集前先停止。

### 9. Single Acquisition（single）— 单次采集（采一帧自动停）

- **作用**：进入"等一次触发"状态，**条件满足才采**，采完**自动 STOP**（定格）。
- **SCPI**：`:SINGle`
- **⚠️ 关键点**：
  - **触发电平必须在信号幅值范围内**，否则一直等不到触发（采不到帧）。
  - 电平是**全局参数**，必须在 single 之前用 Set Edge Trigger Level 设好（`single` 步骤本身没有电平输入框——那是设计如此，不是缺失）。
  - 采完定格后，紧跟 Measure Item 测的就是这一帧，**不会被后续采集覆盖**（比 Force Trigger 稳）。
- **用法**：`Set Edge Trigger Level(1.5V) → Single Acquisition → Measure Item(VPP/FREQ...)`。

### 10. Force Trigger（forceTrigger）— 强制触发

- **作用**：**无视触发条件**，立即触发一次。
- **SCPI**：`:TFORce`
- **⚠️ 关键点**：
  - **不会暂停/定格**——触发一次后**继续运行**（不是 Single！）。想要定格用 Single，或 Force Trigger 后手动 Stop。
  - **不返回数据**——outputs 是空的，只代表"命令发出成功"。要拿数值，必须在它之后用 Measure Item 主动读。
- **用途**：NORM 模式屏幕冻结时强制刷新一帧；信号不在触发范围内但想看波形时。
- **坑**：force 采完继续跑，如果 force 和 measure 之间又有采集命令，帧会被覆盖——要稳定测量用 Single。

---

## 四、通道配置

### 11. Enable Channel（enableChannel）— 通道开关（显示）

- **作用**：开启/关闭通道**显示**。
- **SCPI**：`:CHANnel<n>:DISPlay ON/OFF`
- **输入**：channel（CH1~CH4 下拉）、enable（ON/OFF 下拉）。
- **⚠️ 关键点**：`DISPlay OFF` **只隐藏波形，不关闭采集/测量**！测量命令指定了 CH1 作为源，即使显示关了照样测得到电压——这是示波器的正常语义。
- **用途**：屏幕上只看需要的通道；节省显示资源。
- **要实现"测不到"**：Measure Item 的 source 不填该通道，而不是关显示。

### 12. Set Channel Coupling（setChannelCoupling）— 通道输入耦合

- **作用**：设置信号进入 ADC 前的**处理方式**。
- **SCPI**：`:CHANnel<n>:COUPling AC|DC|GND`
- **输入**：channel、coupling（AC/DC/GND 下拉）。
- **选项**：DC（原样过，默认）/ AC（滤掉直流，只看交流）/ GND（短接到地，0V 直线）。
- **测量影响**：✅ 会变——AC 下 VMAX/VMIN 变成 ±交流摆幅而非绝对电平。
- **用途**：DC 测绝对电压/方波；AC 测电源纹波；GND 校准零位。

### 13. Set Probe Attenuation（setProbe）— 探头衰减比

- **作用**：告诉示波器"探头把信号缩了几倍"，示波器**乘回来**保证读数正确。
- **SCPI**：`:CHANnel<n>:PROBe 0.01~1000`
- **输入**：channel、attenuation（0.01~1000，默认 10）。
- **⚠️ 关键点**：**只告诉示波器比值，不改探头物理分压**——设置必须和实际探头匹配（10X 探头设 10），否则所有电压读数差 10 倍。
- **测量影响**：✅ 设错差 10 倍。标准无源探头用 10（验证序列实测 VPP≈3.08V 就是 10X）。

### 14. Set Voltage Per Division（setVoltageDiv）— 垂直灵敏度（V/div）

- **作用**：设置屏幕纵向"每格多少伏"，只影响**显示比例**。
- **SCPI**：`:CHANnel<n>:SCALe`（单位 V/div）
- **输入**：channel、voltsPerDiv。
- **测量影响**：❌ **不影响**——测量是仪器对采集数据直接算的，跟放大倍数无关。
- **用途**：波形太小调小（放大）、削顶调大（缩小）；测数值不用管它。

### 15. Set Channel Offset（setChannelOffset）— 通道垂直偏移

- **作用**：把整条波形**上下平移**（以伏特为单位）。
- **SCPI**：`:CHANnel<n>:OFFSet`
- **输入**：channel、offset（V）。
- **⚠️ 测量影响**：✅ **影响**——仪器把采集值"减去偏移"再显示/测量。偏移 +1V 时 3V 信号测出 VMAX≈2V。
- **用途**：放大看大信号上的小纹波；多通道波形上下分开。
- **坑**：**测电压前务必确认偏移为 0**，否则读数偏。reset 后默认 0V。

### 16. Set Channel Invert（setInvert）— 通道反相

- **作用**：把波形**上下翻转**（×-1）。
- **SCPI**：`:CHANnel<n>:INVert ON/OFF`
- **输入**：channel、invert（OFF/ON 下拉）。
- **测量影响**：✅ VMAX/VMIN 符号互换（3V↔0V）；**VPP/VRMS 不变**（RMS 是平方均值）。
- **用途**：负向信号显示成正向便于观察/触发；差分测量相位对齐。

### 17. Set Bandwidth Limit（setBandwidthLimit）— 通道带宽限制

- **作用**：限制通道带宽，滤掉高频分量。
- **SCPI**：`:CHANnel<n>:BWLimit OFF|20M`
- **输入**：channel、limit（OFF/20M 下拉）。
- **用途**：信号本身低频但带高频噪声时，开 20M 滤噪（类似 AC 耦合的滤波思路）。

---

## 五、时基

### 18. Set Timebase Scale（setTimebaseScale）— 水平时基（s/div）

- **作用**：设置屏幕横向"每格多少秒"，决定一屏显示多少波形周期。
- **SCPI**：`:TIMebase:MAIN:SCALe`（单位 s/div）
- **输入**：secondsPerDiv。
- **测量影响**：❌ 数值本身不变（但 FREQ/PERIOD 的测量精度依赖时基与采样率配合）。
- **用途**：1kHz 方波用 1ms/div（一屏约 10 周期）；想看清细节调小。

---

## 六、触发

> **触发模型**：触发电平/源/斜率是**全局参数**，与 RUN/STOP/SINGLE 模式无关。设置命令随时可发，Single 用"当前生效的触发条件"。

### 19. Set Trigger Mode（setTriggerMode）— 触发类型

- **作用**：选择触发类型（边沿/脉宽/毛刺/窗口/斜率/视频/码型/延时/超时/持续/建立保持/RS232/IIC/SPI）。
- **SCPI**：`:TRIGger:MODE EDGE|PULSe|RUNT|WIND|SLOPe|VIDeo|PATTern|DELay|TIMeout|DURation|SHOLd|RS232|IIC|SPI`
- **输入**：mode（14 种下拉）。
- **日常**：`EDGE`（边沿触发，默认）足够。

### 20. Set Trigger Sweep（setTriggerSweep）— 触发扫描模式

- **作用**：设置触发等待策略。
- **SCPI**：`:TRIGger:SWEep AUTO|NORMal|SINGle`
- **输入**：sweep（AUTO/NORMal/SINGle 下拉）。
- **选项**：AUTO（不满足就自动扫，波形持续显示）/ NORMal（**不满足就冻结**，屏是死的）/ SINGle（采一帧停）。
- **坑**：NORM 下条件不满足屏幕冻结——用 Force Trigger 强制刷新，或改 AUTO。

### 21. Set Trigger Coupling（setTriggerCoupling）— 触发耦合

- **作用**：设置触发电路的耦合方式。
- **SCPI**：`:TRIGger:COUPling AC|DC|LFReject|HFReject`
- **输入**：coupling（AC/DC/LFReject/HFReject 下拉）。
- **选项**：AC（默认）/ DC / LFReject（滤低频，抗电源噪声干扰触发）/ HFReject（滤高频）。

### 22. Set Edge Trigger Source（setEdgeTriggerSource）— 边沿触发源

- **作用**：选择边沿触发用哪个通道。
- **SCPI**：`:TRIGger:EDGe:SOURce CHANnel<n>`
- **输入**：channel（CH1~CH4 下拉）。
- **要点**：测哪个通道就选哪个通道作为触发源。

### 23. Set Edge Trigger Level（setEdgeTriggerLevel）— 边沿触发电平

- **作用**：设置触发点电压——信号**越过这个电平**的瞬间触发。
- **SCPI**：`:TRIGger:EDGe:LEVel`（单位 V）
- **输入**：level（V）。
- **⚠️ 最关键的一个参数**：
  - 电平必须设在**信号幅值范围内**（CAL 方波 0~3V 用 **1.5V 中点**最稳）。
  - **电平 0V 贴在方波底部** → 触发点不稳定 → 测量全 9.9E37（"无有效测量"哨兵值），并污染后续 Single。这是全项目踩得最深的一个坑。
  - 改变电平会改变**触发时刻**，波形在屏幕上水平平移（正常现象，不是信号变了）。
- **用法**：测方波前先设 1.5V，再 Run/Single。

### 24. Set Edge Trigger Slope（setEdgeTriggerSlope）— 触发边沿

- **作用**：选择在信号的**上升沿/下降沿/双边沿**触发。
- **SCPI**：`:TRIGger:EDGe:SLOPe POSitive|NEGative|RFALl`
- **输入**：slope（Rising/Falling/Either 下拉）。
- **选项**：Rising（上升沿，默认）/ Falling（下降沿）/ Either（双边）。

---

## 七、测量

### 25. Measure Item（measure）— 测量项

- **作用**：读取一个测量项的值（VPP/VMAX/VMIN/VTOP/VBASe/VAVG/VRMS/FREQ/PERIOD/RTIME/FTIME/PWIDTH/NWIDTH/PDUTY/NDUTY/DELAY/PHASE）。
- **SCPI**：`:MEASure:ITEM? <item>,CHANnel<n>`
- **输入**：item（17 项下拉）、source（Default/CH1~CH4）、source2（Not Used/CH1~CH4）。
- **⚠️ 关键点**：
  - **数据在 outputs.value**，不是 measure 的 outcome（outcome 只代表命令成功）。
  - `item` 已改为**下拉框**，天然禁绑变量——之前字符串框被误绑成 `${step:x.outcome}` → 发 `:MEASure:ITEM? Passed` → 超时（已修复）。
  - **测的是仪器当前保留的那一帧**：Single 采完定格测最稳；Force Trigger 采完继续跑，帧可能被覆盖。
  - **9.9E37 = 无有效测量**（触发电平不对/没信号/没采集）——遇到它先查触发电平和采集状态。
  - 普源 PDUTY/NDUTY 返回**小数比例**（如 0.5 表示 50%），Limit 用 0.4~0.6 而不是 40~60。
- **实测参考（CAL 1kHz/3Vpp，10X 探头）**：VPP≈3.08、FREQ≈999.9999、PERIOD≈0.001s、VRMS≈2.10、VMAX≈3.0、VMIN≈-0.08、PWIDTH≈0.0005s。

---

## 八、自动设置

### 26. Auto Scale（autoScale）— 自动设置

- **作用**：自动调整垂直/水平/触发电平，让信号一屏显示清晰（**面板 AUTO 键的远程等价物**）。
- **SCPI**：`:AUToscale`
- **⚠️ 关键点**：
  - **处理后仪器忙**（和 *RST 同类），紧跟查询会超时——序列里加 wait 或 retry。
  - 可能**改变已有的测量/触发设置**——需要精确控制时用手动配置（Set Voltage Per Division/Set Timebase Scale/Set Edge Trigger Level），不要用 Auto Scale。
- **用途**：手动看波形时快速恢复显示；自动化测试里一般不用（要精确配置）。

---

## 九、自定义指令

### 27. Send SCPI Query（query）— 带返回的自定义指令

- **作用**：写一条 SCPI 查询并读取响应。
- **SCPI**：任意查询命令（如 `:CHANnel1:DISPlay?`）。
- **输入**：command。
- **用途**：验证某个设置是否生效（回读状态）、调试新命令。验证序列里大量用它回读通道/触发状态。

### 28. Send SCPI Command（write）— 直接指令（无返回）

- **作用**：写一条 SCPI 设置命令，不读响应。
- **SCPI**：任意设置命令（如 `:RUN`）。
- **输入**：command。
- **用途**：下发插件未封装的命令、批量设置。

---

## 附：测量影响速查表

| UI 功能名（id） | 作用 | 影响测量值吗 |
|---|---|---|
| Set Channel Coupling | 信号处理方式 | ✅ VMAX/VMIN 变 |
| Set Probe Attenuation | 探头倍数校准 | ✅ 设错差 10 倍 |
| Set Voltage Per Division | 纵向放大 | ❌ 不影响 |
| Set Channel Offset | 上下平移 | ✅ 读数减偏移 |
| Set Channel Invert | 上下翻转 | ✅ VMAX/VMIN 互换（VPP/VRMS 不变）|
| Set Bandwidth Limit | 滤高频 | ⚠️ 高频分量被滤后相关项变 |
| Set Timebase Scale | 横向比例 | ❌ 数值不变 |
| Set Edge Trigger Level | 触发点 | ⚠️ 设错→9.9E37（无有效测量）|

## 附：验证序列覆盖

`C:/Work/PicoATE/projects/SCOPE/rigol_scope_full_verify_sequence.json`（104 步 / 28 函数全覆盖）实测 104/104 PASS ×3。SCOPE-T 为简化调试版序列。新增功能后记得同步补序列步骤，保持"功能=验证"闭环。
