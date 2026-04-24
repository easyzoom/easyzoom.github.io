<template>
  <ClientOnly>
    <ParticleBackground />
    <Layout>
      <template #doc-footer-before>
        <Copyright
          v-if="(frontmatter?.aside ?? true) && (frontmatter?.showArticleMetadata ?? true) && !(frontmatter.authorLink)"
          :key="md5(page.relativePath)" />
      </template>
      <template #doc-after>
        <Comment v-if="(theme.commentConfig?.showComment ?? true) && (frontmatter?.showComment ?? true)"
          :commentConfig="theme.commentConfig" :key="md5(page.relativePath)" />
      </template>
      <template #layout-bottom>
        <Footer v-if="!hasSidebar && (theme.footerConfig?.showFooter ?? true) && (frontmatter?.showFooter ?? true)" />
      </template>
    </Layout>
    <PixelPet />
    <MermaidZoom ref="mermaidZoomRef" />
  </ClientOnly>
</template>

<script lang="ts" setup>
  import { computed, ref, onMounted, onUnmounted } from 'vue';
  import DefaultTheme from 'vitepress/theme';
  import { useData } from 'vitepress';
  import md5 from 'blueimp-md5';
  import Copyright from './components/layout/Copyright.vue';
  import Comment from './components/layout/Comment.vue';
  import Footer from './components/layout/Footer.vue';
  import PixelPet from './components/PixelPet.vue';
  import ParticleBackground from './components/ParticleBackground.vue';
  import MermaidZoom from './components/MermaidZoom.vue';

  const { Layout } = DefaultTheme;
  const { page, theme, frontmatter } = useData();
  const mermaidZoomRef = ref<InstanceType<typeof MermaidZoom> | null>(null);
  const hasSidebar = computed(() => {
    return (
      frontmatter.value.aside !== false && frontmatter.value.layout !== 'home'
    )
  });

  /**
   * 为所有 mermaid 图表添加点击放大事件
   *
   * mermaid 插件 (vitepress-plugin-mermaid) 渲染的 DOM 结构:
   *   <div class="mermaid" v-html="svgCode">
   *     <svg id="dmermaid-xxx" ...>...</svg>
   *     <span style="display:none">salt</span>
   *   </div>
   *
   * 注意: 外层 div 没有 id 属性（id 只传给了 mermaid render() 函数）。
   *       因此用 div.mermaid 作为选择器。
   */
  function bindMermaidZoom() {
    // 选择器: 所有 class="mermaid" 的 div（排除已被标记的）
    document.querySelectorAll('.mermaid:not([data-mzoom])').forEach(container => {
      (container as HTMLElement).dataset.mzoom = '1';
      (container as HTMLElement).style.cursor = 'zoom-in';

      container.addEventListener('click', (e) => {
        e.preventDefault();
        e.stopPropagation();
        mermaidZoomRef.value?.open(container as HTMLElement);
      });
    });
  }

  let observer: MutationObserver | null = null;

  onMounted(() => {
    // 使用 MutationObserver 监听子树变化（childList），捕获 mermaid 异步渲染
    observer = new MutationObserver(() => {
      bindMermaidZoom();
    });
    observer.observe(document.body, { childList: true, subtree: true });

    // 同时在 mounted 后延迟绑定（兜底）
    setTimeout(bindMermaidZoom, 500);
  });

  onUnmounted(() => {
    observer?.disconnect();
  });
</script>

<style scoped></style>
