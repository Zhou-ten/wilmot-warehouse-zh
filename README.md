# Wilmot's Warehouse 简体中文 Mod

适用于 **Steam Windows x64 版 1.0.5**。v0.4.0 加入 A 组操作说明图片和按钮的简体中文，并包含 v0.3.1 的负重教程修复。游戏中按 **F8** 可立即切换菜单、教程、对话和操作说明的语言，下次启动沿用选择。

## 下载和安装

1. 退出游戏。原版或使用 `version.dll` 的 v0.3 公开版选用 [v0.4.0 安装包](https://github.com/Zhou-ten/wilmot-warehouse-zh/raw/refs/heads/main/Wilmot-Warehouse-ZH-v0.4.0.zip)。已安装早期替换版、游戏目录中已有 `wilmzh.dll` 的玩家选用 [v0.4.0 旧版升级包](https://github.com/Zhou-ten/wilmot-warehouse-zh/raw/refs/heads/main/Wilmot-Warehouse-ZH-v0.4.0-legacy-update.zip)。
2. 在 Steam 游戏属性里打开「已安装文件 → 浏览」。把压缩包里的 `WilmotPC` 文件夹复制到打开的 `Wilmot's Warehouse` 根目录，选择合并。
3. 从 Steam 正常启动。首次默认中文，按 **F8** 切换。

在仓库靠近《重要须知》后打开操作说明，按 **F8** 可在当前页往返切换。键鼠说明页已在 2560×1440 实测，标题、标签、正文和返回按钮同步切换。

公开版的 `version.dll`、`wilmzh-font.wzh` 和 `wilmzh-help.wzh` 应与 `Wilmot.exe` 位于同一目录。旧版升级包则覆盖 `wilmzh.dll` 并加入 `wilmzh-help.wzh`。合并后从 Steam 启动即可。

卸载公开版时先退出游戏，再移除其三个 Mod 文件。早期替换版请使用旧版备份还原。`WilmotPC/.wilmzh-cache`、`wilmzh.ini` 和 `wilmzh.log` 是本机生成的字体缓存、语言偏好和日志，可一并移除。

## 源码和资源

可下载 [v0.4.0 完整源码](https://github.com/Zhou-ten/wilmot-warehouse-zh/raw/refs/heads/main/wilmot-warehouse-zh-source-v0.4.0.zip)。

`src/` 是游戏内运行的 DLL 源码，`translations/zh-CN.json` 是可编辑的翻译表，`assets/` 中是中文字形和操作说明的文字补丁。英文使用原版 DIN 字形，中文用字重相近的 Noto Sans SC。说明图的图标和背景由游戏本机资源绘制，安装 ZIP 仅包含 Mod 文件。

## 开发者构建

在 Windows 上使用 x64 C++17 编译器、CMake 3.20+ 和 Python 3 构建 DLL：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
python tools/package.py --dll build/version.dll --output dist/Wilmot-Warehouse-ZH-v0.4.0.zip
```

构建旧版升级包时，使用单独的构建目录，配置时加上 `-DWILMZH_LEGACY_LOADER=ON`，并将生成的 `wilmzh.dll` 传给打包脚本。

若修改了翻译中的字符，使用自己的原版游戏目录和 [Noto Sans SC 可变 TTF](https://github.com/google/fonts/blob/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf) 重新生成补丁：

```powershell
python -m pip install -r requirements.txt
python tools/build_assets.py --original "C:\path\to\WilmotPC" --noto-font "C:\path\to\NotoSansSC-VF.ttf"
python tools/build_help_assets.py --original "C:\path\to\WilmotPC" --noto-font "C:\path\to\NotoSansSC-VF.ttf"
```

发布前运行 `python tools/audit_public.py` 检查已暂存文件。此 Mod 的原创代码与翻译贡献采用 [MIT](LICENSE)。第三方许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
