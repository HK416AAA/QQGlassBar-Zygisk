# QQGlassBar-Zygisk

QQ 悬浮底栏 + 液态玻璃模块（Zygisk 注入，minSdk 31 = Android 12+）。

## 组成
- `zygisk/`：单载荷 Zygisk 容器（QQ/TIM，主进程+:MSF/:peak）
- `core/` + `native/`：单一 LSPlant 引擎（`core.dex` + `libmq-core.so`，Dobby 静态）
- `payload/`：QQGlassBar 载荷工程（`me.glassbar.hook.GlassBarEntry`）
  - 默认 framework 自绘（SDK36 可编）；
  - 液态玻璃效果：在 `payload/build.gradle.kts` 放开
    `io.github.kyant0:backdrop` 依赖并在 SDK37(android-37) 下构建（参照 QFun `hook/ui/LiquidGlassTabBar*` 挂载 QQ 主界面）。
- `scripts/assemble-module.sh`、`.github/workflows/build.yml`：打包与 SDK37 CI

## 状态
- 骨架与容器可编译；挂载 QQ 主界面/真液态玻璃渲染需真机定位（QQ 9.x 容器、层级、模糊 API），见 `GlassBarEntry.installUiHook()` 的 TODO。
