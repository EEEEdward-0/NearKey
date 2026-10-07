# NearKey / 近钥

用手机、手表或其他蓝牙设备离开电脑时自动锁屏，回来后可以在登录界面快速解锁。

NearKey 不会替换 Windows 或 Linux 原来的 PIN、密码登录。第一次安装和遇到问题时，都可以照常用密码登录。

## 功能

- 蓝牙设备离开后自动锁定电脑。
- 支持蓝牙、局域网，或两者同时作为判断条件。
- 最多添加 8 台蓝牙设备。
- “试运行”可以先看看设置是否合适，不会真的锁屏或登录。
- 关闭设置窗口后，后台仍会继续运行。

## 项目目录

- `app/`：NearKey 设置窗口、后台服务和构建脚本
- `demo/`：Windows 登录组件和安装脚本（目录名沿用早期项目名称）
- `linux/`：Ubuntu 服务、登录模块和安装脚本
- `docs/`：开发记录和测试说明
- `release/`：本地生成的压缩包，不会提交到源码仓库
## 下载和安装

### 直接下载

打开 [v1.0.0 Release](https://github.com/EEEEdward-0/NearKey/releases/tag/v1.0.0)，下载对应系统的压缩包：

- [Windows x64](https://github.com/EEEEdward-0/NearKey/releases/download/v1.0.0/NearKey-windows-x64.zip)：大多数 Intel/AMD Windows 电脑
- [Windows ARM64](https://github.com/EEEEdward-0/NearKey/releases/download/v1.0.0/NearKey-windows-arm64.zip)：Windows ARM64 电脑
- [Ubuntu x64 / ARM64](https://github.com/EEEEdward-0/NearKey/releases/download/v1.0.0/NearKey-linux-x64-arm64.zip)：Ubuntu 两种架构

下载后解压，再按照下面的 Windows 或 Linux 说明操作。

### 用 Git 获取项目

如果电脑已经安装 Git，可以这样下载源码：

```powershell
git clone https://github.com/EEEEdward-0/NearKey.git
cd NearKey
git checkout v1.0.0
```

Git 下载的是源码和安装脚本，不会自动安装程序。仍然需要从上面的 Release 下载对应平台的压缩包；如果要自己编译，请看文末的“从源码构建”。
## Windows

支持 Windows 11 x64 和 ARM64。

1. 下载对应架构的压缩包并解压。
2. 运行密码配置程序，在本机输入 Windows 账户密码。
3. 以管理员身份运行 `app/install-app.ps1`。
4. 打开“近钥 NearKey”，在“设备”页添加手机或其他蓝牙设备。
5. 在“解锁设置”里设置阈值和离开延迟并保存。
6. 先用“试运行”看看设置，再按 `Win + L` 测试。

窗口关闭不会停止后台服务。需要停止或卸载时，运行项目中的卸载脚本。

## Linux

支持 Ubuntu x64 和 ARM64，兼容 GNOME、KDE Plasma（SDDM）和 LightDM 的登录方式。

先保留可用的密码登录，再进行下面的操作：

1. 复制配置文件：

   ```sh
   sudo cp linux/nearkey.conf.example /etc/nearkey.conf
   ```

2. 编辑配置，填写 Linux 用户名和蓝牙地址，并确保文件只能由 root 读取：

   ```sh
   sudo chmod 600 /etc/nearkey.conf
   ```

3. 安装服务和 PAM 模块：

   ```sh
   sudo sh linux/install.sh
   ```

4. 查看状态：

   ```sh
   sudo /usr/local/sbin/nearkey status
   ```

5. 确认服务正常后，再启用登录界面接入：

   ```sh
   sudo sh linux/enable-pam.sh
   ```

6. 预演规则：

   ```sh
   sudo /usr/local/sbin/nearkey preview -60 0
   ```

卸载：

```sh
sudo sh linux/uninstall.sh
```

Linux 的详细配置见 [linux/README.md](linux/README.md)。真实 GNOME、KDE 或 LightDM 登录界面还需要在目标 Ubuntu 设备上自行测试。

## 使用提醒

NearKey 根据蓝牙信号和可选的局域网状态判断设备是否靠近。这些信号有可能被伪造，所以请把它当作方便登录的工具，不要当作强身份认证。请始终保留 PIN 或密码登录。

密码不会写进项目文件、命令行或日志。Linux PAM 配置和状态文件会限制为 root 使用。

## 和普通蓝牙自动锁定工具的区别

NearKey 会显示当前找到的设备和判断结果，也可以先用“试运行”看看效果。自动锁定和登录解锁分开设置，Linux 登录界面需要另外启用。

## 从源码构建

Windows x64：

```powershell
& .\app\build.ps1
```

Windows ARM64：

```powershell
& .\app\build-arm64.ps1
```

Linux x64 / ARM64：

```powershell
& .\linux\build.ps1 -Zig 'C:\path\to\zig.exe'
```

构建结果位于 `dist/`。Windows 需要 Visual Studio C++ Build Tools、Windows SDK 和 .NET SDK。

## 当前状态

- Windows x64：已构建并检查本机安装版。
- Windows ARM64：已完成交叉编译，需要 ARM64 实机做最终登录测试。
- Linux x64 / ARM64：已完成交叉编译，需要 Ubuntu 实机测试各桌面环境。

遇到问题时，先关闭自动解锁，使用原有 PIN 或密码登录，再查看设置界面的运行记录。




