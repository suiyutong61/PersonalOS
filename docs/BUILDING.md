# 构建与测试

## 支持环境

当前维护环境：Windows 11、Qt 6.11.2、Qt MinGW 13.1 64-bit、CMake。CMake 配置要求 Qt 6.10 或更高版本。

需要的 Qt 组件：Quick、Sql、Network、Test。

## 命令行构建

```powershell
$env:PATH='D:\QT\6.11.2\mingw_64\bin;D:\QT\Tools\mingw1310_64\bin;' + $env:PATH
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH='D:\QT\6.11.2\mingw_64'
cmake --build build --parallel 1
```

Windows 会锁定正在运行的可执行文件。重新链接前应结束应用：

```powershell
Get-Process appPersonOS -ErrorAction SilentlyContinue | Stop-Process -Force
```

不要对同一个构建目录同时运行多个 CMake build；这可能造成截断的对象文件。

## 自动测试

```powershell
Set-Location build
ctest --output-on-failure
```

QML/无窗口检查：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
.\appPersonOS.exe --ui-check
```

若 Qt Framework 环境要求许可检查旁路，可仅在受控开发环境为当前进程设置：

```powershell
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK='1'
```

## 运行时部署

从构建目录之外运行独立 exe 时，需要部署 Qt 运行库：

```powershell
windeployqt --qmldir .\qml .\build\appPersonOS.exe
```

发布前应在一台没有 Qt 开发环境的干净 Windows 用户环境中验证启动、数据库初始化和 QML 插件加载。

## 测试分级

- **编译通过**：只说明代码能生成目标文件。
- **定向测试通过**：只覆盖相关模块。
- **完整 CTest 通过**：覆盖仓库登记的自动测试。
- **ui-check 通过**：覆盖 QML 载入与基本页面构造。
- **真实模型测试**：覆盖第三方 API、结构化输出和失败处理。
- **人工验收**：覆盖可用性、窗口行为和真实工作流。

交付说明必须准确标注实际完成的层级。

## 常见问题

### `Permission denied` 或无法替换 exe

结束所有 `appPersonOS.exe`、`mingw32-make`、`g++`、`ld` 进程后重试。

### `file truncated`

通常是并发构建同一目录造成的损坏。停止构建进程，删除受影响目标的中间产物或重新创建构建目录，然后串行重建。

### 启动提示缺少 Qt DLL

使用匹配当前 Qt 套件的 `windeployqt`，不要从其他 Qt 版本手工混拷 DLL。
