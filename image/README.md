# MessageBox 图片目录

把流程提示图放在本目录中，重新编译后会复制到 `PicoATE.UI.exe` 同级的 `image` 目录。

- 支持 `.png`、`.jpg` 和 `.jpeg`。
- Flow Editor 的 MessageBox 参数会自动列出该目录中的图片。
- Sequence JSON 只保存文件名，例如 `fixture_connection.png`，便于整包复制到其他电脑。
- 也可以直接把图片放进发布工具包的 `image` 目录，无需重新编译。
