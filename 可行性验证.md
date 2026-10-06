# 蓝牙自动解锁：第一轮可行性验证

日期：2026-10-06

## 已确认的范围

- Windows 11 家庭版 25H2；当前登录账户为 Microsoft 账户。
- 只使用 iPhone 和 Apple Watch；设备端不安装应用，也不增加专用信标。
- 第一版只处理已登录账户的锁屏，不处理开机或重启后的首次登录。
- 多选设备时，以当前检测到的设备的 RSSI 均值判断；只检测到一台时仍可解锁。
- 项目文件不保存账户密码；用户已在本机交互窗口输入密码，demo 使用机器范围 DPAPI 加密并存于仅允许 SYSTEM 和管理员读取的注册表项。

## 本机只读验证

- 蓝牙适配器正常运行，Windows 已有一部配对的 iPhone。
- 桌面扫描 15 秒：发现 20 台附近蓝牙设备，其中配对 iPhone 的广播 RSSI 为约 -56 dBm。
- 用户确认电脑全程锁屏的 40 秒测试：每个 5 秒区间都收到该 iPhone 广播；各区间测得的 RSSI 范围合计约 -60 至 -46 dBm。
- 手表在附近时，扫描到多个没有名称的 Apple 厂商广播；目前无法证明哪一个属于该 Apple Watch。用户暂时无法做移远对照测试。
- 扫描只读取广播，没有改动配对、登录或系统安全设置。

## Windows 接口结论

- Windows 提供 BLE 广播 RSSI 读取接口；RSSI 受环境影响，不能当作精确距离。
- Windows 提供第三方 Credential Provider；它可以向登录界面提交凭据，并支持尝试自动登录。
- Windows 官方文档说明，在系统认为不合适的情形下，自动登录可能仍出现“登录”按钮。实际能否在这台 Windows 11 25H2 电脑上做到完全无操作解锁，尚未验证。
- 旧的 Windows Hello Companion Device Framework 已弃用，不宜作为第一版依赖。
- 已在 D 盘独立目录安装 Rust GNU 构建工具，并成功编译一个公开的 Credential Provider 示例作为工具链检查。该示例仅针对本地账户且自述可能有错误，不会用于本机登录，也没有复制进应用项目。

## 2026-10-06 最小 demo 验收结果

- 已安装独立的 Windows 登录磁贴，并保留原有 PIN／密码登录方式。
- iPhone BLE 监听器通过本机命名管道把最近一次达标信号交给登录组件；普通用户和 SYSTEM 身份的读取探针均通过，停止监听器后探针拒绝解锁。
- 在真实 Windows 锁屏界面，用户选中 **Bluetooth Unlock Demo** 并点击 **Login** 后成功进入桌面。
- 原先的“信号太旧”由 SYSTEM 无法读取用户注册表状态引起，改用命名管道后解决；随后发现首次保存的密码与实际 Windows 密码不同，重新在本机配置并验证后完成解锁。

## 后续验收项

> 以下清单是最小 demo 阶段的历史记录。2026-10-06 的后续开发已在真实锁屏界面验证：安装版后台运行且 iPhone 靠近时，按 `Win + L` 后无需点击即可进入桌面。多设备均值规则、设置界面和登录后自启已实现，详见 `app/README.md`。自动锁定及 Apple Watch 独立识别仍待实测。

1. **iPhone 长时间稳定性**：目前只有短时间锁屏验证；需覆盖屏幕熄灭、手机锁屏、蓝牙重连以及地址变化。
2. **Apple Watch 身份识别**：需在 iPhone 留在电脑旁时，让手表单独移远，再对照广播；如果无法稳定识别，不能将它列为可靠可选设备。
3. **无操作解锁**：目前需要选择磁贴并点击 **Login**；完全无操作进入桌面尚未验证。
4. **防误触与安全性**：RSSI 和广播标识本身不是加密身份验证；上线前需要验证误解锁风险和凭据保护。

当前状态：最小 demo 已完成“iPhone 靠近、锁屏点击 Login 后进入桌面”的实机验证。Apple Watch、多设备均值、自动锁定、设置界面与开机自启尚未实现。

## 官方资料

- [Windows BLE 广播与 RSSI](https://learn.microsoft.com/en-us/windows/uwp/devices-sensors/ble-beacon)
- [Windows Credential Providers](https://learn.microsoft.com/en-us/windows/win32/secauthn/credential-providers-in-windows)
- [Credential Provider 自动登录行为](https://learn.microsoft.com/en-us/windows/win32/api/credentialprovider/nf-credentialprovider-icredentialprovidercredential-setselected)
- [Windows Hello Companion Device Framework 弃用说明](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/windows-hello-companion-device-framework)
