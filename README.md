# AppLock

Windows Hello 应用锁，包含 C++ 核心、Node.js 本地管理服务和 SVG 管理界面。

## 使用

1. 安装 Node.js 18 或更高版本。
2. 在项目目录运行 `npm install`。
3. 双击 `启动应用锁.cmd`。
4. 管理页面地址为 `http://127.0.0.1:3000/Applock.html`。

首次提交的 `config.json` 为空，不包含任何本机应用路径。请在管理页面中添加需要保护的程序。

## 构建

使用 Visual Studio 打开 `AppLockDemo.sln`，选择 `Release | x64`。构建后的核心程序放在项目根目录的 `AppLock.exe`。

## 目录说明

- `main.cpp`：窗口监控和 Windows Hello 认证核心。
- `server.js`：本地配置服务和核心进程生命周期管理。
- `public/Applock.html`：管理界面。
- `config.json`：本机配置文件，已提供空白模板。
