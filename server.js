const express = require('express');
const fs = require('fs');
const path = require('path');

const bodyParser = require('body-parser');

const app = express();
const PORT = 3000;
const CONFIG_PATH = path.join(process.env.APPLOCK_DATA_DIR || __dirname, 'config.json');

// 中间件
app.use((req, res, next) => {
    const origin = req.headers.origin;
    if (origin && origin !== 'http://127.0.0.1:3000' && origin !== 'http://localhost:3000') return res.sendStatus(403);
    if (!['127.0.0.1:3000', 'localhost:3000'].includes(req.headers.host)) return res.sendStatus(403);
    next();
});
app.use(bodyParser.json());
app.use(express.static(path.join(__dirname, 'public')));
app.get('/Applock.html', (req, res) => res.sendFile(path.join(__dirname, 'public', 'control.html')));

// 初始化配置文件
function initConfig() {
    if (!fs.existsSync(CONFIG_PATH)) {
        const defaultConfig = {
            apps: [
                { id: 1001, name: "微信", path: "C:\\Program Files\\Tencent\\WeChat.exe", locked: true, process: "WeChat.exe" },
                { id: 1002, name: "Microsoft Edge", path: "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", locked: true, process: "msedge.exe" },
                { id: 1003, name: "系统设置", path: "ms-settings:", locked: false, process: "SystemSettings.exe" }
            ]
        };
        fs.writeFileSync(CONFIG_PATH, JSON.stringify(defaultConfig, null, 2));
    }
}
initConfig();

// 读取配置
function readConfig() {
    try {
        const data = fs.readFileSync(CONFIG_PATH, 'utf8');
        return JSON.parse(data);
    } catch (error) {
        console.error('读取配置文件失败:', error);
        throw new Error('配置文件损坏，请检查 config.json；未覆盖原有配置');
    }
}

// 写入配置
function writeConfig(config) {
    try {
        fs.writeFileSync(CONFIG_PATH + '.tmp', JSON.stringify(config, null, 2));
        fs.renameSync(CONFIG_PATH + '.tmp', CONFIG_PATH);
        return true;
    } catch (error) {
        console.error('写入配置文件失败:', error);
        return false;
    }
}

// ---------- API 路由 ----------

// 获取所有应用
app.get('/api/apps', (req, res) => {
    const config = readConfig();
    res.json(config.apps);
});

// 添加新应用
app.post('/api/apps', (req, res) => {
    const { name, path: appPath, locked = true } = req.body;
    if (typeof name !== 'string' || !name.trim() || typeof appPath !== 'string' || !appPath.trim() || typeof locked !== 'boolean') {
        return res.status(400).json({ error: '应用名称不能为空' });
    }

    const config = readConfig();
    const newId = Date.now() + Math.floor(Math.random() * 1000);
    const newApp = {
        id: newId,
        name: name.trim(),
        path: appPath?.trim() || "自定义路径",
        locked: locked,
        process: path.basename(appPath || '') || name
    };

    config.apps.push(newApp);
    if (writeConfig(config)) {
        res.status(201).json(newApp);
    } else {
        res.status(500).json({ error: '保存失败' });
    }
});

// 更新应用锁定状态
app.patch('/api/apps/:id', (req, res) => {
    const appId = parseInt(req.params.id);
    const { locked } = req.body;

    if (typeof locked !== 'boolean') {
        return res.status(400).json({ error: '缺少 locked 字段' });
    }

    const config = readConfig();
    const app = config.apps.find(a => a.id === appId);
    if (!app) {
        return res.status(404).json({ error: '应用不存在' });
    }

    app.locked = locked;
    if (writeConfig(config)) {
        res.json(app);
    } else {
        res.status(500).json({ error: '保存失败' });
    }
});

// 删除应用
app.delete('/api/apps/:id', (req, res) => {
    const appId = parseInt(req.params.id);
    const config = readConfig();
    const initialLength = config.apps.length;
    config.apps = config.apps.filter(a => a.id !== appId);

    if (config.apps.length === initialLength) {
        return res.status(404).json({ error: '应用不存在' });
    }

    if (writeConfig(config)) {
        res.json({ success: true });
    } else {
        res.status(500).json({ error: '删除失败' });
    }
});


const { exec, spawn } = require('child_process');

app.get('/api/processes', (req, res) => {
    // PowerShell 命令：获取进程名和可执行文件路径
    const cmd = `powershell -Command "Get-Process | Where-Object {$_.Path} | Select-Object ProcessName, Path | ConvertTo-Json"`;

    exec(cmd, { maxBuffer: 1024 * 1024 * 10 }, (error, stdout, stderr) => {
        if (error) {
            console.error('获取进程列表失败:', error);
            return res.status(500).json({ error: '获取进程列表失败' });
        }
        try {
            const processes = JSON.parse(stdout);
            // 统一处理：PowerShell 可能返回单个对象而非数组
            const processArray = Array.isArray(processes) ? processes : processes ? [processes] : [];

            // 转换为前端需要的格式：名称、路径
            const result = processArray.map(p => ({
                name: p.ProcessName,
                path: p.Path
            })).filter(p => p.path); // 过滤掉没有路径的进程

            res.json(result);
        } catch (e) {
            console.error('解析进程列表 JSON 失败:', e);
            res.status(500).json({ error: '解析进程数据失败' });
        }
    });
});

// Core lifecycle is serialized; configuration edits are read by the core directly.
const { execFile } = require('child_process');
const appLockExePath = path.join(__dirname, 'AppLock.exe');
let core = null;
let restarting = null;
let shuttingDown = false;
function startCore() {
    return new Promise((resolve, reject) => {
        if (core) return resolve();
        const child = spawn(appLockExePath, [CONFIG_PATH], { cwd: __dirname, windowsHide: true, stdio: 'ignore' });
        core = child;
        let settled = false;
        const timer = setTimeout(() => { settled = true; resolve(); }, 500);
        child.once('error', error => { clearTimeout(timer); if (core === child) core = null; reject(error); });
        child.once('exit', code => {
            clearTimeout(timer); if (core === child) core = null;
            if (!settled) reject(new Error(`核心启动失败 (${code})，请关闭其他版本的应用锁后重试`));
            else if (!shuttingDown && !restarting) console.error('应用锁核心已退出，请重新启动。');
        });
    });
}
function stopCore() {
    const child = core;
    if (!child) return Promise.resolve();
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('核心关闭超时，未强制终止，以保留隐藏窗口的恢复机会')), 8000);
        child.once('exit', () => { clearTimeout(timer); resolve(); });
        execFile(appLockExePath, ['--stop'], { windowsHide: true }, error => {
            if (error) { clearTimeout(timer); reject(error); }
        });
    });
}
app.get('/api/status', (req, res) => res.json({ app: 'AppLockDemo', version: 3, coreRunning: !!core, shuttingDown, configPath: CONFIG_PATH }));
app.get('/api/hello-status', (req, res) => {
    execFile(appLockExePath, ['--hello-status'], { windowsHide: true, timeout: 10000 }, error => {
        res.json({ available: !error, message: !error ? 'Windows Hello 可用' : 'Windows Hello 不可用或检测失败，请检查系统登录选项' });
    });
});
app.post('/api/restart', async (req, res) => {
    if (shuttingDown) return res.status(409).json({ error: '正在关闭' });
    if (!restarting) restarting = (async () => { await stopCore(); await startCore(); })().finally(() => { restarting = null; });
    try { await restarting; res.json({ success: true, message: '应用锁已重启' }); }
    catch (error) { res.status(500).json({ error: error.message }); }
});
async function shutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    try {
        if (restarting) await restarting.catch(() => {});
        await stopCore();
        server.close(() => process.exit(0));
        server.closeIdleConnections();
    } catch (error) { shuttingDown = false; throw error; }
}
app.post('/api/shutdown', async (req, res) => {
    if (shuttingDown) return res.json({ success: true, message: '正在关闭' });
    try {
        shuttingDown = true;
        if (restarting) await restarting.catch(() => {});
        await stopCore();
        res.json({ success: true, message: '窗口已恢复，应用锁已关闭' });
        server.close(() => process.exit(0));
        server.closeIdleConnections();
    } catch (error) { shuttingDown = false; res.status(500).json({ error: error.message }); }
});
app.use((error, req, res, next) => res.status(500).json({ error: error.message }));
const server = app.listen(PORT, '127.0.0.1', async () => {
    try {
        await startCore();
        console.log(`应用锁已启动：http://127.0.0.1:${PORT}/Applock.html`);
        if (process.env.APPLOCK_NO_BROWSER !== '1') exec('start "" "http://127.0.0.1:3000/Applock.html"');
    } catch (error) { console.error(error.message); server.close(() => process.exit(1)); }
});
server.on('error', error => { console.error(error.message); process.exitCode = 1; });
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => shutdown().catch(error => console.error(error.message)));
