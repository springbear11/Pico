# PicoATE Skills

PicoATE 产线测试相关的 AI 技能包，按 Codex/OpenAI skill 约定组织：
每个 skill 一个独立子目录，共享资料放在 `shared/`。

## 目录结构

```text
picoskill/
├── README.md                     # 本索引
├── shared/                       # 多个 skill 共用的参考资料
│   └── 寄存器映射表_协议合并版.md   # 盛弘充电桩 Modbus TCP 寄存器映射
└── skills/                       # 每个 skill 一个子目录
    ├── pico-log-analysis/        # 日志分析：结合协议表定位失败与假 PASS
    ├── modbus-config/            # Modbus 脚本配置（规划中）
    ├── can-config/               # CAN 脚本配置（规划中）
    ├── interaction-patterns/     # 人机交互模式：notice提示+状态位轮询+judgment判断
    ├── register-config-import/   # 寄存器表配置导入：配置表→三层 TestItem JSON
    ├── teststand-migration/      # TestStand 项目迁移为 PicoATE 脚本
    └── plugin-writing/           # 仪器插件编写：抽象接口+桥接+厂商实现+注册表部署+验证序列
```

## Skill 清单

| Skill | 用途 | 状态 |
|-------|------|------|
| [pico-log-analysis](./skills/pico-log-analysis/SKILL.md) | 分析 PicoATE/ATE 自动测试日志，结合 Modbus 协议寄存器表、sequence JSON 和执行引擎语义定位通信失败、地址/寄存器偏移错误、引用错误、解析异常、测不过原因和假 PASS | 已启用 |
| [modbus-config](./skills/modbus-config/SKILL.md) | Modbus 脚本配置规范与校验 | 可用 |
| [can-config](./skills/can-config/SKILL.md) | CAN 脚本配置规范与校验 | 规划中 |
| [interaction-patterns](./skills/interaction-patterns/SKILL.md) | 人机交互模式：notice 提示 + 状态位轮询（retry 50/500ms）+ judgment 判断框的决策规则与标准结构 | 已启用 |
| [register-config-import](./skills/register-config-import/SKILL.md) | 寄存器表配置导入：把机型寄存器配置表转换为三层 TestItem JSON（不依赖小工具），含换算/unitId/插入规则 | 已启用 |
| [teststand-migration](./skills/teststand-migration/SKILL.md) | 把 NI TestStand 项目迁移为 PicoATE JSON 脚本（含 Step/VI 映射、语义转换、合并脚本防坑） | 已启用 |
| [plugin-writing](./skills/plugin-writing/SKILL.md) | 编写/部署/调试 PicoATE 仪器插件（抽象接口+桥接+厂商实现+注册表+站点+验证序列），含 VISA 层与真机调试坑位 | 已启用 |

## 新增 Skill 的约定

每个新 skill 必须遵循以下结构（参考 `pico-log-analysis` 的现有实现）：

```text
skills/<skill-name>/
├── SKILL.md                      # 必选：skill 主定义（frontmatter 声明 name/description）
├── agents/                       # 可选：agent 接口声明（如 openai.yaml）
└── references/                   # 可选：配套参考资料（如检查表）
```

### SKILL.md frontmatter 规范

```yaml
---
name: <skill-name>              # 小写连字符，如 modbus-config
description: <一句话说明该 skill 何时使用，给代理判断用>
---
```

### 约定

- **共享资料只放一份**：多 skill 共用的协议表、规范等放 `shared/`，不要在每个 skill 里复制。
- **每个 skill 只做一件事**：日志分析、Modbus 配置、CAN 配置分开，不要揉进一个 SKILL.md。
- **检查表与主文件分离**：长清单放 `references/`，SKILL.md 保持聚焦。
- **描述要写触发条件**：`description` 写明"何时 Use this skill"，便于代理自动选择。
- **安全边界**：分析类 skill 默认只读；涉及实机写入的 skill 必须要求先提示地址、值和恢复方式。

## 使用

把本目录（或 `skills/` 下需要的子目录）提供给代理。示例：

```text
Use pico-log-analysis to analyze this PicoATE log against its sequence and register map.
```
