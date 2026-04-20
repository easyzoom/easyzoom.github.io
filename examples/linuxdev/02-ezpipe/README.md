# 02 ezpipe

字符设备最小读写示例，对应两篇文章：

- 《字符设备驱动模型与设备节点》
- 《open、read、write、release 的最小实现》

## 编译驱动

```bash
make
```

## 加载驱动

```bash
sudo insmod ezpipe.ko
dmesg | tail
ls -l /dev/ezpipe
```

## 编译并运行用户态测试

```bash
make user
./user_ezpipe
```

## 卸载

```bash
sudo rmmod ezpipe
dmesg | tail
```
