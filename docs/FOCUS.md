# 专注卡 / Focus timer

适用 **2026.11.28 定版**，应用内版本 **2026.11.28-computer**。此前五卡固件没有本功能。

从底部点击“专注”，或左右滑动进入第六张卡。

1. 选择 **25、45 或 60 分钟**；也可点“自定义”，输入 **1–999 的整数分钟**。
2. 自定义键盘支持数字、清空、退格。点击“确认时长”设置；点击“取消”或关闭按钮保留原时长。键盘显示时，底层卡片、导航、滑动与时钟待机均不可操作。
3. 点击“开始”计时，按钮变为“暂停”；暂停后点“继续”接着计时。“结束”回到所选时长的未开始状态。运行或暂停中不能修改时长，先结束再选。

B 布局用大数字和薄荷绿进度条显示剩余时间，卡片内没有额外小猫。设备本地独立计时，不需要电脑、联网、账号或新传感器。切换卡片、蓝牙/Wi-Fi 断开后仍继续计时。**运行时禁止自动和手动进入时钟待机，暂停或结束后恢复原待机规则。**

到时由屏幕上原有行走小猫的“休息一下”气泡及专注导航点提醒，不强行切卡；普通三分钟待机计时重新开始。进入专注卡会确认提醒，完成页面保留 `00:00`，可以再来一轮或结束。配置/键盘弹窗优先，关闭后仍保留未确认的提醒。

计时只保存在内存中，**重启/断电后回到默认 25 分钟**。不会自动开始休息计时，也不修改真实音乐、系统音量或电脑设置。网页预览有独立模拟状态，不是设备遥控器。

绿色 K2 键盘基于项目已有 [LVGL 9.3 Keyboard](https://lvgl.io/docs/open/9.3/details/widgets/keyboard)，使用 [MIT 许可](https://github.com/lvgl/lvgl/blob/v9.3.0/LICENCE.txt)，定制纯数字键图及圆角皮肤。

## English

Release **2026.11.28** (embedded app **2026.11.28-computer**) provides the sixth Focus card; older five-card releases do not include it.

Choose **25 / 45 / 60 minutes**, or **自定义** (custom) for an integer **1–999 minutes**. The green K2 keypad offers digits, clear and backspace. Confirm sets the duration without starting; cancel/close preserves the previous value. Its full-screen overlay blocks background cards, navigation, gestures and standby.

**开始** starts, **暂停** pauses, **继续** resumes and **结束** ends the session. End a running or paused session before changing its duration. The large digits and mint progress bar replace the old ring and in-card cat. Only the existing roaming cat remains.

The local timer continues across cards and without PC/BLE/Wi-Fi connectivity. **Running sessions block automatic and manual clock standby; paused or ended sessions permit it.** Completion produces a cat bubble and navigation dot without forcing navigation, and restarts the normal three-minute inactivity window. Visiting Focus acknowledges the reminder; the completed card stays at `00:00` until restart/end. Dialogs take priority while a reminder remains pending.

Session state is RAM-only: **reboot/power-off resets to an idle 25-minute preset**. It does not automatically start a break or control real PC media/settings. The website has its own demo timer, not a live device connection. The keypad uses the existing MIT-licensed LVGL 9.3 keyboard with a custom numeric map and rounded green theme.
