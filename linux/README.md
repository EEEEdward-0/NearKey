# NearKey / 近钥：Ubuntu x64 与 ARM64

Linux 版通过 BlueZ 读取 BLE 实时信号，经 `loginctl` 自动锁定用户会话，并通过 PAM 在锁屏登录界面放行用户确认后的解锁。支持 GNOME 的 GDM、KDE Plasma 的 SDDM，以及 LightDM。PAM 规则放在原有密码规则之前；信号缺失、过期或不足时继续走系统密码登录。后台不保存 Linux 登录密码。

解锁模式包括 `bluetooth`（仅蓝牙）、`lan`（仅局域网）和 `both`（两者同时满足）。自动锁定始终只根据蓝牙离开规则。Linux 版不会在没有用户操作时自行越过登录界面；需在登录界面发起一次认证。

## 安装

要求：Ubuntu、BlueZ 的 `bluetoothctl`、systemd/logind、`ping`，以及上述任一显示管理器。蓝牙设备必须广播电脑可持续看到的稳定地址。轮换私有地址的设备不能仅凭显示的 MAC 安全绑定。

1. 先确认 Linux 密码仍可正常登录。解压 [Linux 发行包](../release/NearKey-linux-x64-arm64.zip)，进入解压后的目录。
2. 以 root 身份把 `linux/nearkey.conf.example` 复制为 `/etc/nearkey.conf`，权限设为 `0600`，填写实际 Linux 用户名和蓝牙地址。选择 `lan` 或 `both` 时，还需配置一至两个 `IPv4@Wi-Fi-MAC`；设置中的示例有格式说明。
3. 执行 `sudo sh linux/install.sh`。脚本按 CPU 架构选择二进制，启动服务并确认其写出状态；这一步不会修改 PAM。确认服务稳定、并保留可用的密码登录方式后，再执行 `sudo sh linux/enable-pam.sh`，只为当前显示管理器加入一条 `sufficient` 规则。原 PAM 文件备份为 `.nearkey-backup`。
4. 执行 `/usr/local/sbin/nearkey status` 查看实时状态。`sudo /usr/local/sbin/nearkey preview -60 1` 会模拟第一台已选蓝牙设备为 -60 dBm、局域网在线一台，只计算结果，不锁屏也不解锁。没有配置局域网设备时，使用 `preview -60 0`。
5. 在锁屏界面分别测试设备靠近、移远及正常密码登录。

`sudo sh linux/uninstall.sh` 会移除服务和 PAM 接入，并保留配置文件与备份供检查。

## 安全与兼容边界

root 后台写入 `/run/nearkey/status`。PAM 模块仅接受属于 root、不能由组或其他用户修改、用户名匹配且 12 秒内更新的状态文件；扫描失败或规则不满足时不会放行。BLE 地址、RSSI 以及局域网 ARP 均可能伪造，因此这是便捷登录机制，不能当作强身份认证。

局域网条件在探测配置的 IP 后核对本机 ARP 表中的 Wi-Fi MAC。Windows 版的 DHCP 后按 MAC 找回 IP 和 WPF 设置界面尚未移植。两个 Linux 架构已交叉编译，**还未在 Linux 实机上验证 GDM、SDDM、LightDM 的锁屏登录**；安装前请保留可用的密码登录方式。

