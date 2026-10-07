# NearKey / 近钥

NearKey 是一个用手机、手表或其他 BLE 设备帮助你保护电脑的工具：设备离开时自动锁定，回到电脑前后，在系统登录界面由你主动确认即可尝试解锁。

它适合经常离开座位、希望减少重复输入密码，又不愿关闭 Windows 原有密码或 PIN 登录方式的人。

## 它能做什么

- 监听附近 BLE 广播，根据最近信号判断设备是否在附近。
- 支持最多 8 台已选设备，并按实际收到的信号计算平均值。
- 设备离开超过设定时间后自动锁定 Windows 会话。
- 登录界面保留原有 PIN 和密码入口；NearKey 只在你主动按确认键或点击解锁磁贴时尝试登录。
- 可选择三种解锁条件：仅蓝牙、仅局域网设备在线、蓝牙和局域网同时满足。
- 局域网模式支持一至两台设备，并用 IPv4 与 Wi-Fi MAC 的 ARP 应答确认在线状态。
- 设置界面带有“安全预演”：输入假设的信号和局域网状态，查看会通过哪条规则；预演不会锁屏，也不会登录。
- 关闭设置窗口后，后台服务和自动锁定继续运行；可以从系统托盘重新打开设置或立即锁定。

## 安全边界

NearKey 使用 BLE 地址、RSSI 和可选的局域网 ARP 信息判断“是否靠近”。这些信息可能被伪造，所以它是便捷登录工具，不是强身份认证系统。请保留 Windows PIN 或密码，并在首次安装和每次规则修改后实际测试普通登录路径。

NearKey 不把密码放入项目文件、命令行或日志。Windows 的已保存密码由本机 DPAPI 保护，并限制为绑定账户、管理员和 SYSTEM 读取；登录组件只接受已注册的 SYSTEM 后台服务提供的状态。Linux PAM 模块只接受 root 拥有、近期更新且用户名匹配的状态文件。

## Windows 安装

适用于 Windows 11 x64 和 ARM64。仓库中的构建脚本会生成对应架构的程序；本地打包文件位于 `release/`，公开仓库发布时应作为 GitHub Release 附件上传，而不是提交到源码历史。

安装前请确认仍能用 Windows PIN 或密码登录。首次配置需要管理员权限：

1. 解压对应架构的压缩包。
2. 运行其中的密码配置程序，在本机交互窗口输入当前 Windows 账户密码。不要把密码复制到聊天、脚本或命令行参数中。
3. 以管理员身份运行 `app/install-app.ps1`，或按项目中的安装说明完成安装。
4. 打开“近钥 NearKey”，在“设备”页选择收到实时 BLE 信号的设备并添加。
5. 在“解锁设置”页设置解锁阈值、锁定阈值、离开延迟和自动锁定开关，然后保存。
6. 先使用“安全预演”检查规则，再将手机放在电脑旁，按 `Win + L` 锁屏并测试。普通 PIN 或密码始终可以作为备用登录方式。

Windows 后台以服务方式运行，登录后另有会话任务执行自动锁定。窗口关闭不会停止后台；要停止或卸载，请使用项目中的卸载脚本。

## Linux 安装

Linux 发行包支持 Ubuntu x64 和 ARM64，构建结果位于 `dist/linux-x64` 和 `dist/linux-arm64`；公开仓库发布时应作为 GitHub Release 附件上传。

Linux 版使用 BlueZ 扫描 BLE，通过 `loginctl` 自动锁定用户会话，并通过 PAM 接入 GDM、KDE Plasma 的 SDDM 或 LightDM。正常密码登录会保留。Linux 版本尚未在真实 GNOME、KDE 或 LightDM 锁屏上完成实机验收，请务必先保留可用的密码登录方式。

基本流程：

1. 解压发行包，复制 `linux/nearkey.conf.example` 为 `/etc/nearkey.conf`。
2. 将配置文件设为 root 拥有、权限 `0600`，填写 Linux 用户名和稳定的 BLE 地址。
3. 运行 `sudo sh linux/install.sh`。这一步只安装服务和 PAM 模块，不修改登录链。
4. 查看服务状态：`/usr/local/sbin/nearkey status`。
5. 确认服务稳定后，再运行 `sudo sh linux/enable-pam.sh` 为当前显示管理器启用登录接入。原 PAM 文件会保存为 `.nearkey-backup`。
6. 用 `sudo /usr/local/sbin/nearkey preview -60 0` 预演规则，再分别测试设备靠近、移远和密码登录。

停止并移除 Linux 接入：

```sh
sudo sh linux/uninstall.sh
```

更完整的 Linux 说明、配置格式和安全限制见 [linux/README.md](linux/README.md)。

## 为什么和同类项目不同

NearKey 的重点是“可解释、可回退、可审查”：

- 每次状态页都会显示蓝牙检测数量、平均信号、局域网在线数量和当前通过条件。
- 安全预演复用实际后台判定函数，在动作发生前说明为什么会通过或被阻止。
- 自动锁定和登录解锁分开处理，设备离线时不会把旧状态当成仍然靠近。
- Windows 和 Linux 都保留原有密码登录路径；Linux PAM 接入分成安装和启用两步，避免未经验证就改动登录配置。

## 从源码构建

Windows x64：

```powershell
& .\app\build.ps1
```

Windows ARM64：先安装 Visual Studio 2022 的 ARM64 C++ 工具组件，再运行：

```powershell
& .\app\build-arm64.ps1
```

Linux x64 和 ARM64 交叉构建需要 Zig：

```powershell
& .\linux\build.ps1 -Zig 'C:\path\to\zig.exe'
```

构建产物会写入 `dist/linux-x64` 和 `dist/linux-arm64`。Windows 依赖 Visual Studio C++ Build Tools、Windows SDK 10.0.26100.0 和 .NET 10 SDK。

## 当前验证状态

- Windows x64：已构建并更新本机安装版；服务、登录任务、规则测试和安全预演均已检查。
- Windows ARM64：后台、登录组件、密码配置程序和 WPF 界面已交叉编译；需要 ARM64 实机做最终登录验收。
- Linux x64 / ARM64：后台和 PAM 模块已交叉编译；需要 Ubuntu 实机做 GDM、SDDM 或 LightDM 锁屏验收。

如果 NearKey 不能满足条件，请直接使用原有 PIN 或密码登录。遇到问题时，先关闭自动解锁，保留密码登录，再查看设置界面的“运行记录”。
