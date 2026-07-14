# ESP32-S3-WROOM-1 入门项目

## 硬件信息
- 芯片: ESP32-S3-WROOM-1 (N8R8 - 8MB Flash, 8MB PSRAM)
- RGB LED: GPIO38 (WS2812)
- BOOT按钮: GPIO0

## 功能说明
- RGB LED循环变色（红、绿、蓝、黄、青、紫、白）
- 每秒切换一次颜色
- 串口输出当前RGB值和BOOT按钮状态
- 波特率: 115200

## 使用方法（VS Code + ESP-IDF插件）

### 1. 打开项目
用VS Code打开 `d:\esp32\client` 文件夹

### 2. 选择目标芯片
- 按 `Ctrl+Shift+P` 打开命令面板
- 输入 `ESP-IDF: Set Espressif Device Target`
- 选择 `esp32s3`

### 3. 选择串口
- 按 `Ctrl+Shift+P`
- 输入 `ESP-IDF: Select Port to Use`
- 选择你的开发板对应的COM口

### 4. 编译项目
- 点击底部状态栏的 **Build** 按钮
- 或按 `Ctrl+Shift+P` → `ESP-IDF: Build Project`

### 5. 烧录固件
- 点击底部状态栏的 **Flash** 按钮
- 或按 `Ctrl+Shift+P` → `ESP-IDF: Flash Device`

### 6. 监视串口输出
- 点击底部状态栏的 **Monitor** 按钮
- 或按 `Ctrl+Shift+P` → `ESP-IDF: Monitor Device`

## 项目结构
```
client/
├── CMakeLists.txt          # 顶层CMake配置
├── sdkconfig.defaults      # 默认SDK配置
├── main/
│   ├── CMakeLists.txt      # main组件配置
│   └── main.c              # 主程序代码
└── .vscode/                # VS Code配置（ESP-IDF插件自动生成）
```

## 常见问题

### 找不到串口
- 确保USB线是数据线（不是充电线）
- 安装CP210x或CH340驱动
- 设备管理器中查看COM口号

### 烧录失败
- 按住BOOT键再按一下RST键进入下载模式
- 确认串口选择正确
- 降低波特率重试

### 编译错误
- 确保ESP-IDF插件已正确安装
- 运行 `ESP-IDF: Configure ESP-IDF Extension` 重新配置
