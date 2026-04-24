<template>
  <Teleport to="body">
    <Transition name="mzoom-fade">
      <div v-if="visible" class="mzoom-overlay" @click.self="close">
        <div class="mzoom-toolbar">
          <button class="mzoom-btn" @click="zoomIn" title="放大">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35M11 8v6M8 11h6"/></svg>
          </button>
          <span class="mzoom-label">{{ Math.round(scale * 100) }}%</span>
          <button class="mzoom-btn" @click="zoomOut" title="缩小">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35M8 11h6"/></svg>
          </button>
          <button class="mzoom-btn" @click="resetZoom" title="重置">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/></svg>
          </button>
          <button class="mzoom-btn mzoom-close-btn" @click="close" title="关闭 (Esc)">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6L6 18M6 6l12 12"/></svg>
          </button>
        </div>

        <div
          class="mzoom-viewport"
          @wheel.prevent="onWheel"
          @mousedown="onPointerDown"
          @touchstart.prevent="onTouchStart"
          @touchmove.prevent="onTouchMove"
          @touchend="onPointerUp"
        >
          <div ref="innerRef" class="mzoom-inner" :style="transformCSS">
            <!-- 镜像 DOM，而非 v-html 字符串，避免 SVG 样式丢失 -->
          </div>
        </div>

        <div class="mzoom-hint">点击空白关闭 · 滚轮缩放 · 拖拽平移 · Esc 退出</div>
      </div>
    </Transition>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'

const visible = ref(false)
const innerRef = ref<HTMLElement | null>(null)
const scale = ref(1)
const tx = ref(0)
const ty = ref(0)

// --- 拖拽状态 ---
let dragging = false
let sx = 0, sy = 0, stx = 0, sty = 0
let touchId: number | null = null

const transformCSS = computed(() =>
  `transform:translate(${tx.value}px,${ty.value}px) scale(${scale.value})`
)

function open(sourceEl: HTMLElement) {
  const clone = sourceEl.cloneNode(true) as HTMLElement

  // 强制 SVG 在放大层中自适应且不超出
  const svg = clone.querySelector('svg')
  if (svg) {
    const vb = svg.getAttribute('viewBox')
    if (vb) {
      const [, , vbW, vbH] = vb.trim().split(/\s+/).map(Number)
      if (vbW && vbH && (isFinite(vbW)) && (isFinite(vbH))) {
        svg.setAttribute('width', String(vbW))
        svg.setAttribute('height', String(vbH))
      }
    }
    svg.style.maxWidth = 'none'
    svg.style.maxHeight = 'none'
  }

  scale.value = 1
  tx.value = 0
  ty.value = 0
  document.body.style.overflow = 'hidden'

  // 先让 DOM 渲染出来（v-if="visible"），再插入克隆节点
  // 否则 innerRef 还是 null（Teleport + v-if 的 DOM 不存在时 ref 挂不上）
  visible.value = true
  nextTick(() => {
    const inner = innerRef.value
    if (!inner) return
    inner.innerHTML = ''
    inner.appendChild(clone)
  })
}

function close() {
  visible.value = false
  document.body.style.overflow = ''
}

function zoomIn()  { scale.value = Math.min(scale.value + 0.25, 5) }
function zoomOut() { scale.value = Math.max(scale.value - 0.25, 0.25) }
function resetZoom() { scale.value = 1; tx.value = 0; ty.value = 0 }

function onWheel(e: WheelEvent) {
  scale.value = Math.min(Math.max(scale.value + (e.deltaY < 0 ? 0.15 : -0.15), 0.25), 5)
}

// --- 鼠标拖拽 ---
function onPointerDown(e: MouseEvent) {
  dragging = true; sx = e.clientX; sy = e.clientY; stx = tx.value; sty = ty.value
  const move = (ev: MouseEvent) => { if (!dragging) return; tx.value = stx + ev.clientX - sx; ty.value = sty + ev.clientY - sy }
  const up   = () => { dragging = false; window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up) }
  window.addEventListener('mousemove', move)
  window.addEventListener('mouseup', up)
}
// --- 触摸拖拽 ---
function onTouchStart(e: TouchEvent) { const t = e.touches[0]; if (!t) return; touchId = t.identifier; dragging = true; sx = t.clientX; sy = t.clientY; stx = tx.value; sty = ty.value }
function onTouchMove(e: TouchEvent) { if (!dragging || touchId === null) return; const t = Array.from(e.touches).find(tt => tt.identifier === touchId); if (!t) return; tx.value = stx + t.clientX - sx; ty.value = sty + t.clientY - sy }
function onTouchEnd() { dragging = false; touchId = null }

function onKey(e: KeyboardEvent) {
  if (!visible.value) return
  if (e.key === 'Escape') close()
  if (e.key === '+' || e.key === '=') zoomIn()
  if (e.key === '-') zoomOut()
  if (e.key === '0') resetZoom()
}
onMounted(() => window.addEventListener('keydown', onKey))
onUnmounted(() => { window.removeEventListener('keydown', onKey); document.body.style.overflow = '' })

defineExpose({ open })
</script>

<style scoped>
.mzoom-overlay {
  position: fixed; inset: 0; z-index: 99999;
  background: rgba(0,0,0,.72);
  backdrop-filter: blur(6px);
  display: flex; align-items: center; justify-content: center;
  cursor: zoom-out;
}
.mzoom-viewport {
  cursor: grab; overflow: hidden;
  width: 100%; height: 100%;
  display: flex; align-items: center; justify-content: center;
}
.mzoom-viewport:active { cursor: grabbing }
.mzoom-inner {
  transition: transform .12s ease-out;
  will-change: transform;
}
/* 让克隆进来的 mermaid 容器在放大层中正常显示 */
.mzoom-inner :deep(.mermaid) {
  background: var(--vp-c-bg, #fff) !important;
  padding: 16px !important;
  border-radius: 8px;
}
.mzoom-inner :deep(svg) {
  display: block !important;
}

/* ---- 工具栏 ---- */
.mzoom-toolbar {
  position: fixed; top: 16px; left: 50%; transform: translateX(-50%);
  display: flex; align-items: center; gap: 4px;
  padding: 5px 10px; border-radius: 10px;
  background: rgba(255,255,255,.12); backdrop-filter: blur(14px);
  z-index: 100000;
}
html:not(.dark) .mzoom-toolbar { background: rgba(255,255,255,.88); box-shadow: 0 2px 12px rgba(0,0,0,.1) }
.mzoom-btn {
  display: flex; align-items: center; justify-content: center;
  width: 30px; height: 30px; border: none; border-radius: 6px;
  background: transparent; color: var(--vp-c-text-1); cursor: pointer; transition: background .15s;
}
.mzoom-btn:hover { background: rgba(255,255,255,.18) }
html:not(.dark) .mzoom-btn:hover { background: rgba(0,0,0,.06) }
.mzoom-close-btn { color: #f56c6c; margin-left: 2px }
.mzoom-close-btn:hover { background: rgba(245,108,108,.14) }
.mzoom-label { font-size: 12px; min-width: 38px; text-align: center; color: var(--vp-c-text-2); font-variant-numeric: tabular-nums }

.mzoom-hint {
  position: fixed; bottom: 16px; left: 50%; transform: translateX(-50%);
  font-size: 12px; color: rgba(255,255,255,.4); pointer-events: none; user-select: none;
}
html:not(.dark) .mzoom-hint { color: rgba(0,0,0,.3) }

/* ---- 过渡 ---- */
.mzoom-fade-enter-active, .mzoom-fade-leave-active { transition: opacity .2s ease }
.mzoom-fade-enter-active .mzoom-inner, .mzoom-fade-leave-active .mzoom-inner { transition: transform .2s ease }
.mzoom-fade-enter-from, .mzoom-fade-leave-to { opacity: 0 }
.mzoom-fade-enter-from .mzoom-inner { transform: translate(0,0) scale(.8) }
.mzoom-fade-leave-to   .mzoom-inner { transform: translate(0,0) scale(1.1) }
</style>
