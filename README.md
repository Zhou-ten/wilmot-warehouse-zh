# Wilmot's Warehouse 简体中文 Mod

适用于 **Steam Windows x64 版 1.0.5**。菜单、界面、教程和对话使用 AI 简体中文翻译。游戏进行中按 **F8**，当前文字立即在中文和英文之间切换，下次启动沿用选择。海报和图示保留原图。

## 下载和安装

1. 退出游戏。下载 [Wilmot-Warehouse-ZH-v0.3.0.zip](https://github.com/Zhou-ten/wilmot-warehouse-zh/raw/refs/heads/main/Wilmot-Warehouse-ZH-v0.3.0.zip)。
2. 在 Steam 游戏属性里打开「已安装文件 → 浏览」。把压缩包里的 `WilmotPC` 文件夹复制到打开的 `Wilmot's Warehouse` 根目录，选择合并。
3. 从 Steam 正常启动。首次默认中文，按 **F8** 切换。

合并后，`WilmotPC/version.dll` 和 `WilmotPC/wilmzh-font.wzh` 应与 `Wilmot.exe` 位于同一目录。游戏自动加载这两个 Mod 文件，玩家无需编译或运行安装器。已安装早期替换版的玩家，请先用旧版的还原文件恢复原版，再复制这两个文件。

卸载时先退出游戏，再移除这两个 Mod 文件。`WilmotPC/.wilmzh-cache`、`wilmzh.ini` 和 `wilmzh.log` 是本机生成的字体缓存、语言偏好和日志，可一并移除。

## 源码和资源

完整源码和构建工具见 [wilmot-warehouse-zh-source-v0.3.0.zip](https://github.com/Zhou-ten/wilmot-warehouse-zh/raw/refs/heads/main/wilmot-warehouse-zh-source-v0.3.0.zip)。

`src/` 是游戏内运行的 DLL 源码，`translations/zh-CN.json` 是可编辑的翻译表，`assets/wilmzh-font.wzh` 是中文字形补丁。英文继续使用原版 DIN 字形，中文用字重相近的 Noto Sans SC。启动时 DLL 读取玩家的本机字体资源，在缓存中合成字形并将字体读取指向缓存。仓库与安装 ZIP 均不包含游戏 EXE、游戏运行库、原版字体、图片或存档。

## 开发者构建

在 Windows 上使用 x64 C++17 编译器、CMake 3.20+ 和 Python 3 构建 DLL：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
python tools/package.py --dll build/version.dll --output dist/Wilmot-Warehouse-ZH-v0.3.0.zip
```

若修改了翻译中的字符，使用自己的原版游戏目录和 [Noto Sans SC 可变 TTF](https://github.com/google/fonts/blob/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf) 重新生成补丁：

```powershell
python -m pip install -r requirements.txt
python tools/build_assets.py --original "C:\path\to\WilmotPC" --noto-font "C:\path\to\NotoSansSC-VF.ttf"
```

发布前运行 `python tools/audit_public.py` 检查已暂存文件。此 Mod 的原创代码与翻译贡献采用 [MIT](LICENSE)。第三方许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
