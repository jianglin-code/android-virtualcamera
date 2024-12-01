# android-virtualcamera
android云手机虚拟摄像头

VirtualCamera-App --- 安装在手机端的图像采集APP

VirtualCamera --- 安装在云手机系统的native service图像接收程序

hardware/rockchip/camera_vir --- hal camera provider

frameworks/av/services/camera/libcameraservice --- cameraservice

RK3588 Android基线代码：
https://wiki.t-firefly.com/zh_CN/ROC-RK3588S-PC/android_compile_android12.0_firmware.html

云手机项目可实现的功能：

1、设备定制，运行环境可模拟各品牌手机，并且可以通过安兔兔，鲁大师检测，最重要的可以通过微信免密登录，相关方案发表在ICSE'2021 《App's Auto-Login Function Security Testing via Android OS-Level Virtualization》

2、模拟WIFI网络，模拟电源，模拟GPS定位，模拟SIM卡

3、root授权，可对单个APP进行授权

4、虚拟摄像头，实现扫码，人脸识别等功能

