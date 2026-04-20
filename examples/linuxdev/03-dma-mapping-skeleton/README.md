# 03 dma mapping skeleton

这个目录不是面向某块真实硬件的完整驱动，而是用来展示三件事：

- `dma_map_single()` / `dma_unmap_single()` 的生命周期
- `scatterlist` 与 `dma_map_sg()` 的基本组织方式
- DMA 地址与 CPU 指针的职责边界

## 阅读重点

1. `dma_handle` 不是 CPU 可解引用指针
2. `dma_mapping_error()` 必须检查
3. SG 映射后的有效段数要以 `dma_map_sg()` 返回值为准

## 适合对应的文章

- 《DMA基础、物理地址、虚拟地址与总线地址》
- 《cache一致性、streamingDMA与coherentDMA》
- 《IOMMU、ScatterGather与高性能数据通路设计》
