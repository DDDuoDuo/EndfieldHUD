# 原 WatchBlur Temporal 捕获生命周期

原未补丁代码支持单次请求：WatchBlurCtrl.OnCreate 显式 InitRT→Register，把捕获 handle 登记到 HGCamera.m_rtExtractionLists 的 duration=1（Temporal）集合。每次相机渲染记录结束，OnRecordingEnd 清空该 duration 的各个 type 集合；duration=0（Persistent）保持。

清理不是 extractionDoneCallback 完成回调所做。HGCamera 静态构造的默认回调准确解析为 RTExtractionDone；该函数未 IFix 分支直接返回。真正的 OnRecordingEnd 在正常 HGRenderPathBase.OnPostRendering 和 ExecuteRenderRequestCPP 两条路径均有直接调用，后者已独立从完整 PE 函数体确认指令边界。

清空请求不释放捕获 RT。UIBlurRT._autoUpdate=false 的 OnDisable 直接返回；显式 UpdateRT 在已有 handle 时不会再次 InitRT/Register。图像可以继续显示上一次捕获结果。后续显式 Register 仍可更新这个 handle；Scene FrostedGlass 自身缓存是否持续更新是另一条链。

这里只证明原未补丁的静态实现。RecordingEnd 是 CPU 记录阶段，不是 GPU 已完成的时间点；清空请求本身也不能证明每种动态 render gate 下都已调度或成功执行复制。未观测 IFix 活跃补丁、实际 panel 复用/销毁计划或游戏现场像素。因此桌面版在每次打开菜单时捕获一次是明确的输入适配政策，不能把它等同于游戏所有阶段的实测生命周期。

全部方法地址、关键指令、来源文件 SHA256 和重现脚本见 temporal-lifetime.json。没有执行游戏、游戏 DLL 或进程，也没有修改安装文件、生产源码或冻结资源包。
