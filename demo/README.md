# 蓝牙解锁最小 demo

> 此目录保留早期单设备实验流程。当前应用的构建、安装与实测状态请看 `../app/README.md`；下面的点击 Login 步骤记录的是早期 demo，不代表当前安装版的操作方式。

此 demo 用已配对 iPhone 的 BLE 信号作为接近条件。电脑锁屏后，选择 **Bluetooth Unlock Demo**，点击 **Login**；如果最近 45 秒内收到高于阈值的信号，就向 Windows 提交本机配置的 Microsoft 账户密码。Windows 原有 PIN／密码登录方式保持可用。

## 当前组成

- `monitor/`：原生 C++ 蓝牙监听进程，记录最近一次达标的 RSSI，并通过本机命名管道向登录组件提供状态。
- `credential-provider/`：以微软 MIT 许可示例为基础的原生 Windows 登录组件。
- `setup/`：在本机交互输入账户密码，用 Windows DPAPI 机器范围加密后保存；注册表密钥仅允许 SYSTEM 和管理员读取。
- `install-demo.ps1`、`uninstall-demo.ps1`：明确的安装与回退脚本。

## 构建

在装有 Visual Studio 2022 C++ Build Tools 和 Windows SDK 10.0.26100.0 的电脑上，分别用 MSBuild 构建三个 `vcxproj` 的 `Release|x64` 配置。这里的三个项目已在当前电脑编译通过。

## 本机试用顺序

1. **请先确认你仍能通过 Windows PIN 或密码登录。**
2. 以管理员身份打开交互式终端，运行 `setup\x64\Release\BluetoothSetup.exe`，在该终端本机输入并确认 Microsoft 账户密码。不要通过聊天或命令行参数传递密码。
3. 在同一管理员终端运行 `install-demo.ps1`。
4. 在普通用户终端运行 `start-monitor.ps1 -IPhoneAddress <已配对 iPhone 的 12 位蓝牙地址>`。demo 默认阈值为 -65 dBm。
5. 等待监听进程收到手机广播，按 `Win + L` 锁屏，选中 **Bluetooth Unlock Demo** 并点击 **Login**。若失败，用原有 PIN 或密码进入 Windows。
6. 停用时先运行 `stop-monitor.ps1`；卸载时以管理员身份运行 `uninstall-demo.ps1`。

## 目前限制

- 已在本机真实锁屏界面验证：iPhone 在附近时，选中此磁贴并点击 **Login** 可以进入桌面。普通用户和 SYSTEM 身份下都能读取监听器状态；停止监听器后，SYSTEM 探针会拒绝解锁。
- 只支持一部已配对 iPhone；Apple Watch 仍需单独身份验证。
- 早期 demo 流程仅针对锁屏；当前应用已增加首次登录前服务路径，安装与验收状态见 `../app/README.md`。
- 蓝牙地址和 RSSI 可以被伪造。此 demo 仅用于验证体验，不应当作为高安全性身份验证方式。
- 最近信号保留 45 秒；手机离开后，在此时间窗口内仍可能满足解锁条件。
- 监听进程目前需手动启动，尚无设置界面或开机自启；这些留待核心登录链路实机通过后处理。
