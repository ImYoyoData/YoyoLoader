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

## 发布步骤

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

## 发布前检查清单

- [ ] `dev` 工作区干净
- [ ] Windows 构建通过
- [ ] Linux 构建通过
- [ ] 在真实 BDS 上跑过一遍（至少 1.26.52.3）
- [ ] `wiki/versions.md` 的支持矩阵已更新
- [ ] `README.md` 的版本表已更新
- [ ] 未实现项在文档里有明确说明
