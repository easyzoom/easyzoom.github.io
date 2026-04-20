# 01 hello lkm

最小内核模块示例，对应文章《第一个内核模块与 Makefile》。

## 编译

```bash
make
```

## 加载与验证

```bash
sudo insmod hello.ko
dmesg | tail
sudo rmmod hello
dmesg | tail
```
