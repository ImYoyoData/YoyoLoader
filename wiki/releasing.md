# 发布流程

## 概述

发布由 GitHub Actions 自动完成：推送到 `main` 或打 tag 会触发构建，两个平台的
压缩包自动生成并附加到 Release。

## 分支约定

| 分支 | 用途 |
|---|---|
| `dev` | 日常开发，只有这里提交 |
| `main` | 稳定分支，只接受发布合并 |

日常提交只进 `dev`（见 [PROJECT_RULES.md](../PROJECT_RULES.md)）。
用户说「发布版本」时才合并到 `main`。

## 一条命令发布

```bash
git checkout dev
./scripts/release.sh 0.1.0
```

脚本会做完整流程：检查工作区干净 → 检查 tag 不存在 → 拉取 origin →
`dev` 合并到 `main`（`--no-ff`）→ 打 tag `v0.1.0` → 推送 `main` 和 tag。

推送 tag 后 GitHub Actions 自动构建两个平台、打包、创建 Release。

## 手动发布步骤

### 1. 在 dev 上确认状态

```bash
git checkout dev
git status          # 必须干净
```

### 2. 合并到 main

```bash
git checkout main
git merge --no-ff dev -m "Release v0.1.0"
```

### 3. 打 tag 并推送

```bash
git tag -a v0.1.0 -m "YoyoLoader v0.1.0"
git push origin main v0.1.0
```

推送 tag 后，Actions 的 `release` job 会：

1. 构建 Windows（MSVC 2022）
2. 构建 Linux（clang + lld）
3. 打包两个平台
4. 创建 GitHub Release 并附上压缩包与 SHA256

### 4. 手动触发（可选）

Actions → `build-and-release` → Run workflow → 填 `version`（如 `0.1.0`）。
填了版本才会创建 Release，留空只构建并上传 artifact。

## 产物结构

两个压缩包都**直接解压到 BDS 根目录**即可使用。

### Windows：`YoyoLoader-<version>-win64.zip`

```text
yoyo.exe
yoyo_runtime.dll
README.txt
plugins/
  home/        manifest.json + main.js
  title/       manifest.json + main.js
  tpa/         manifest.json + main.js
  sample_cpp/  manifest.json + sample_cpp.dll
```

### Linux：`YoyoLoader-<version>-linux64.tar.gz`

```text
yoyo
libyoyo_runtime.so
README.txt
plugins/
  home/  title/  tpa/  sample_cpp/
```

### 校验

每个压缩包附带 `.sha256` 文件：

```bash
sha256sum -c YoyoLoader-0.1.0-linux64.tar.gz.sha256
```

```powershell
Get-FileHash YoyoLoader-0.1.0-win64.zip -Algorithm SHA256
```

## 使用方式

### Windows

```bat
:: 解压到 BDS 根目录后
yoyo.exe
```

启动器在当前目录查找 `bedrock_server.exe`，注入 `yoyo_runtime.dll` 后运行。

需要 drop-in 替换时：

```bat
copy yoyo.exe bedrock_server_mod.exe
```

### Linux

```bash
chmod +x yoyo
./yoyo
```

启动器设置 `LD_PRELOAD=./libyoyo_runtime.so` 后 `execv` 当前目录的 `bedrock_server`。

## 本地打包（不经过 CI）

Windows：

```bat
scripts\build_windows.bat
scripts\package_windows.bat 0.1.0
```

Linux：

```bash
scripts/build_linux.sh
chmod +x scripts/package_linux.sh
./scripts/package_linux.sh 0.1.0
```

产物在 `dist/`。

## 解压验收测试

发布包必须能在**干净 BDS 目录**里直接运行，不能依赖任何已存在的 yoyo 文件。

### Windows 实测

```text
1. 复制 BDS 到空目录（不含 yoyo.exe / yoyo_runtime.dll）
2. 解压 YoyoLoader-<ver>-win64.zip 到该目录
3. 运行 yoyo.exe
```

结果：

```text
[INFO] module: D:\MyCode\mc\BDS\_accept\win\bedrock_server.exe
[INFO] build id: d9f9837c2108f3824c4c44205044422e1
[INFO] build matched an exact version profile
[INFO] all features enabled for this build
[INFO] inline hook installed tryToLoadPlayer / SetLocalPlayerAsInitialized /
       disconnectClientWithMessage / TextPacket / _onPlayerLeft
[INFO] Level::tick inline hook installed
[INFO] packet factory hook installed
[INFO] plugin loader finished: 1 native, 3 javascript, 0 skipped
```

bot 连接后收到 `§a[YoyoLoader] hello from the C++ sample`，说明注入、事件、
插件全部生效。

### Linux 实测

```text
1. 复制 BDS 到空目录（不含 yoyo / libyoyo_runtime.so）
2. tar -xzf YoyoLoader-<ver>-linux64.tar.gz -C <dir>
3. chmod +x yoyo && ./yoyo
```

结果：

```text
[INFO] build id: 519f9a2c3cdc31c85ef87dd2f128f79136f854d4
[INFO] build matched an exact version profile
[INFO] version support: 1.26.70.21 (linux)
[WARN] limited features on this build: ...
[INFO] inline hook installed SetLocalPlayerAsInitialized rva=0x9199160
[INFO] plugin loader finished: 1 native, 3 javascript, 0 skipped
Quit correctly
```

受限版本上原生插件仍能加载；它调用的 API 会因能力门控返回失败，不会崩溃。

## 已知问题与修复记录

### Linux CI 构建失败（已修复）

**症状**：Windows job 成功，Linux job 在 `Build` 步骤直接失败，没有任何编译错误输出。

**根因**：脚本被以 CRLF 提交。GitHub checkout 到 Linux 后 shebang 变成
`#!/usr/bin/env bash
`，报错：

```text
/usr/bin/env: 'bash
': No such file or directory
```

**修复**：新增 `.gitattributes`，按文件类型强制换行：

```gitattributes
* text=auto eol=lf
*.sh   text eol=lf
*.bat  text eol=crlf
*.exe  binary
```

提交后用 `git add --renormalize .` 重新规范化索引。

### 版本号在两个平台不一致（已修复）

`github.ref_name` 在 tag 推送时是 `v0.1.0`，手动触发时是 `0.1.0`，导致包名不同。
现在由独立的 `prepare` job 统一解析：剥掉 `v` 前缀，两个平台产物同名。

### 编译器版本漂移（已修复）

GitHub runner 上 `clang` 的主版本可能比开发环境新，新警告会改变构建行为。
CI 现在显式安装并切换到 `clang-14` / `lld-14`，与本地验证环境一致。

### Windows 产物校验脚本崩溃（已修复）

第一版校验用了 `[System.IO.Compression.ZipFile]::OpenRead`，在某些 PowerShell
环境下该类型未加载，报 `找不到类型 [System.IO.Compression.ZipFile]`。
改为 `Expand-Archive` 到临时目录后用 `Test-Path` 逐项检查，稳定可靠。

### Linux 校验脚本的空白问题（已修复）

用多行缩进字符串定义必需文件列表时，YAML 会把缩进写进 shell 变量，
导致 `grep -qx` 匹配不到任何文件。现在列表从行首开始，逐行遍历。

### 必须在 CI 里打印构建输出（已修复）

Linux 构建曾在没有任何编译器错误的情况下失败，因为脚本自身没跑起来。
现在 CI 用 `bash -x` 并把最后 120 行输出留在日志里，`exit ${PIPESTATUS[0]}`
保留真实退出码。

### 构建失败时看不到原因（已修复）

`build_linux.sh` 现在会在关键命令失败时打印 `runtime build FAILED` 并立即退出，
CI 也用 `bash -x` 输出完整命令轨迹。

## 验证记录

最近一次 `dev` 推送的 CI 结果：

| Job | 结果 |
|---|---|
| `prepare` | ✅ |
| `build-windows` | ✅ Build / Package / Verify / Upload |
| `build-linux` | ✅ Build / Package / Verify / Upload |
| `release` | ⏭ 跳过（非 tag / 未填版本） |

对应运行：Actions run `40373138583`（`dev` 分支）。

### 产物内容校验

CI 在打包后会检查压缩包必须包含：

```text
yoyo.exe / yoyo
yoyo_runtime.dll / libyoyo_runtime.so
README.txt
plugins/home/manifest.json       plugins/home/main.js
plugins/title/manifest.json      plugins/title/main.js
plugins/tpa/manifest.json        plugins/tpa/main.js
plugins/sample_cpp/manifest.json
```

缺任何一项都会让 job 失败，不会发布残缺的包。

### 换行符防复发

`.gitattributes` 控制索引层，`.editorconfig` 控制编辑器层。两者都存在时，
Windows 上编辑 shell 脚本也不会再写成 CRLF。

如果本地脚本已经变成 CRLF：

```bash
python -c "
from pathlib import Path
for p in Path('scripts').glob('*.sh'):
    b = p.read_bytes()
    if b'

' in b:
        p.write_bytes(b.replace(b'

', b'
'))
        print('fixed', p)
"
```

## 发布前检查清单

- [ ] `dev` 工作区干净
- [ ] Windows 构建通过
- [ ] Linux 构建通过
- [ ] 在真实 BDS 上跑过一遍（至少 1.26.52.3）
- [ ] `wiki/versions.md` 的支持矩阵已更新
- [ ] `README.md` 的版本表已更新
- [ ] 未实现项在文档里有明确说明
