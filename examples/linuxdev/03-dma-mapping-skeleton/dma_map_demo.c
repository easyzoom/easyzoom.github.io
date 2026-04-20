#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

struct ez_dma_demo {
	struct device *dev;
	void *cpu_buf;
	dma_addr_t dma_handle;
	size_t len;
	struct scatterlist sg[2];
};

static int ez_demo_prepare_streaming(struct ez_dma_demo *demo)
{
	demo->dma_handle = dma_map_single(demo->dev, demo->cpu_buf, demo->len,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(demo->dev, demo->dma_handle))
		return -EIO;

	/*
	 * 这里通常会把 dma_handle 写进硬件描述符。
	 * 传输完成后需要调用 dma_unmap_single()。
	 */
	return 0;
}

static void ez_demo_finish_streaming(struct ez_dma_demo *demo)
{
	dma_unmap_single(demo->dev, demo->dma_handle, demo->len, DMA_TO_DEVICE);
}

static int ez_demo_prepare_sg(struct ez_dma_demo *demo, void *a, size_t a_len,
			      void *b, size_t b_len)
{
	int mapped;

	sg_init_table(demo->sg, ARRAY_SIZE(demo->sg));
	sg_set_buf(&demo->sg[0], a, a_len);
	sg_set_buf(&demo->sg[1], b, b_len);

	mapped = dma_map_sg(demo->dev, demo->sg, ARRAY_SIZE(demo->sg),
			    DMA_FROM_DEVICE);
	if (mapped <= 0)
		return -EIO;

	/*
	 * 注意：mapped 可能小于原 scatterlist 项数。
	 * 后续遍历应以 mapped 为准，而不是 ARRAY_SIZE(demo->sg)。
	 */
	return mapped;
}

static void ez_demo_finish_sg(struct ez_dma_demo *demo)
{
	dma_unmap_sg(demo->dev, demo->sg, ARRAY_SIZE(demo->sg), DMA_FROM_DEVICE);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EASYZOOM");
MODULE_DESCRIPTION("DMA mapping skeleton for teaching");
