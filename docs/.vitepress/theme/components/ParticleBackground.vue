<template>
  <canvas ref="canvasRef" class="particle-bg" aria-hidden="true" />
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue';
import { useData } from 'vitepress';

const canvasRef = ref<HTMLCanvasElement>();
const { isDark } = useData();

interface Particle {
  x: number;
  y: number;
  vx: number;
  vy: number;
  radius: number;
  alpha: number;
  alphaDir: number;
}

let ctx: CanvasRenderingContext2D | null = null;
let rafId: number;
let particles: Particle[] = [];
let W = 0;
let H = 0;

// ── 配置 ──────────────────────────────────────────────────
const CONFIG = {
  count: () => Math.min(Math.floor((W * H) / 18000), 80),
  maxDist: 140,
  speed: 0.35,
  radiusMin: 1.2,
  radiusMax: 2.8,
};

// 品牌色 in RGB，与 vars.css 保持一致
const LIGHT_RGB = { r: 99, g: 102, b: 241 };  // #6366f1 indigo
const DARK_RGB  = { r: 129, g: 140, b: 248 };  // #818cf8 lighter indigo

function getColor(alpha: number) {
  const c = isDark.value ? DARK_RGB : LIGHT_RGB;
  return `rgba(${c.r},${c.g},${c.b},${alpha.toFixed(3)})`;
}

// ── 粒子创建 ─────────────────────────────────────────────
function createParticle(): Particle {
  const angle = Math.random() * Math.PI * 2;
  const speed = (Math.random() * 0.5 + 0.15) * CONFIG.speed;
  return {
    x: Math.random() * W,
    y: Math.random() * H,
    vx: Math.cos(angle) * speed,
    vy: Math.sin(angle) * speed,
    radius: Math.random() * (CONFIG.radiusMax - CONFIG.radiusMin) + CONFIG.radiusMin,
    alpha: Math.random() * 0.4 + 0.2,
    alphaDir: Math.random() > 0.5 ? 1 : -1,
  };
}

function initParticles() {
  const n = CONFIG.count();
  particles = Array.from({ length: n }, createParticle);
}

// ── 主绘制循环 ────────────────────────────────────────────
function draw() {
  if (!ctx) return;
  ctx.clearRect(0, 0, W, H);

  const maxDist = CONFIG.maxDist;
  const maxDist2 = maxDist * maxDist;

  // 更新粒子位置
  for (const p of particles) {
    p.x += p.vx;
    p.y += p.vy;

    // 边缘反弹（留 10px 缓冲）
    if (p.x < -10) p.x = W + 10;
    else if (p.x > W + 10) p.x = -10;
    if (p.y < -10) p.y = H + 10;
    else if (p.y > H + 10) p.y = -10;

    // 闪烁
    p.alpha += p.alphaDir * 0.003;
    if (p.alpha > 0.65) { p.alpha = 0.65; p.alphaDir = -1; }
    if (p.alpha < 0.1)  { p.alpha = 0.1;  p.alphaDir = 1; }
  }

  // 绘制连线（仅对 N² 做距离裁剪，数量少时可接受）
  for (let i = 0; i < particles.length; i++) {
    const a = particles[i];
    for (let j = i + 1; j < particles.length; j++) {
      const b = particles[j];
      const dx = a.x - b.x;
      const dy = a.y - b.y;
      const dist2 = dx * dx + dy * dy;
      if (dist2 > maxDist2) continue;

      const ratio = 1 - Math.sqrt(dist2) / maxDist;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.strokeStyle = getColor(ratio * 0.18);
      ctx.lineWidth = ratio * 1.2;
      ctx.stroke();
    }
  }

  // 绘制粒子节点
  for (const p of particles) {
    ctx.beginPath();
    ctx.arc(p.x, p.y, p.radius, 0, Math.PI * 2);
    ctx.fillStyle = getColor(p.alpha);
    ctx.fill();

    // 光晕
    const grd = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, p.radius * 3.5);
    grd.addColorStop(0, getColor(p.alpha * 0.25));
    grd.addColorStop(1, getColor(0));
    ctx.beginPath();
    ctx.arc(p.x, p.y, p.radius * 3.5, 0, Math.PI * 2);
    ctx.fillStyle = grd;
    ctx.fill();
  }

  rafId = requestAnimationFrame(draw);
}

// ── resize ────────────────────────────────────────────────
function resize() {
  if (!canvasRef.value) return;
  W = canvasRef.value.width  = window.innerWidth;
  H = canvasRef.value.height = window.innerHeight;
  initParticles();
}

// 切换暗色时不需要重建粒子，只用重新渲染颜色即可（draw 里动态读 isDark）
watch(isDark, () => {/* draw loop 自动响应 */});

onMounted(() => {
  const canvas = canvasRef.value;
  if (!canvas) return;
  ctx = canvas.getContext('2d');
  resize();
  rafId = requestAnimationFrame(draw);
  window.addEventListener('resize', resize, { passive: true });
});

onUnmounted(() => {
  cancelAnimationFrame(rafId);
  window.removeEventListener('resize', resize);
});
</script>

<style scoped>
.particle-bg {
  position: fixed;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
  z-index: 0;
  /* 亮色模式：极低不透明度，不干扰阅读 */
  opacity: 0.55;
}

/* 暗色模式下稍微亮一点，让粒子更可见 */
:global(html.dark) .particle-bg {
  opacity: 0.75;
}
</style>
