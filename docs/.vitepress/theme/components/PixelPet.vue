<template>
  <div
    ref="petRef"
    class="pixel-pet"
    :class="[state, { dragging }]"
    :style="{ left: x + 'px', bottom: y + 'px' }"
    @mousedown.prevent="startDrag"
    @touchstart.prevent="startDrag"
    @click="handleClick"
    :title="tooltip"
  >
    <svg class="cat-svg" viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg" :style="{ transform: `scaleX(${facing})` }">
      <!-- Tail -->
      <g class="tail-group">
        <path class="tail" d="M15,88 Q-5,70 5,50 Q12,38 20,45" fill="none" stroke="#F4A34D" stroke-width="6" stroke-linecap="round"/>
        <path d="M15,88 Q-5,70 5,50 Q12,38 20,45" fill="none" stroke="#E8923A" stroke-width="2" stroke-linecap="round" stroke-dasharray="0,14,8,100" opacity="0.5"/>
      </g>

      <!-- Body -->
      <ellipse cx="60" cy="88" rx="32" ry="22" fill="#F4A34D"/>
      <ellipse cx="60" cy="88" rx="28" ry="18" fill="#FBD48B" opacity="0.4"/>
      <!-- Body stripes -->
      <path d="M45,76 Q50,80 45,85" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.5" stroke-linecap="round"/>
      <path d="M55,74 Q58,79 55,84" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.5" stroke-linecap="round"/>
      <path d="M65,74 Q68,79 65,84" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.5" stroke-linecap="round"/>
      <path d="M75,76 Q78,80 75,85" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.5" stroke-linecap="round"/>

      <!-- Front paws -->
      <ellipse cx="42" cy="105" rx="10" ry="6" fill="#F4A34D" class="paw-l"/>
      <ellipse cx="78" cy="105" rx="10" ry="6" fill="#F4A34D" class="paw-r"/>
      <ellipse cx="42" cy="106" rx="7" ry="4" fill="#FBD48B" opacity="0.6"/>
      <ellipse cx="78" cy="106" rx="7" ry="4" fill="#FBD48B" opacity="0.6"/>
      <!-- Toe beans -->
      <circle cx="38" cy="107" r="1.5" fill="#FFB6C1" opacity="0.8"/>
      <circle cx="42" cy="108" r="1.5" fill="#FFB6C1" opacity="0.8"/>
      <circle cx="46" cy="107" r="1.5" fill="#FFB6C1" opacity="0.8"/>
      <circle cx="74" cy="107" r="1.5" fill="#FFB6C1" opacity="0.8"/>
      <circle cx="78" cy="108" r="1.5" fill="#FFB6C1" opacity="0.8"/>
      <circle cx="82" cy="107" r="1.5" fill="#FFB6C1" opacity="0.8"/>

      <!-- Head -->
      <circle cx="60" cy="52" r="30" fill="#F4A34D"/>
      <!-- Head lighter center -->
      <ellipse cx="60" cy="56" rx="22" ry="20" fill="#FBD48B" opacity="0.35"/>
      <!-- Forehead stripes -->
      <path d="M52,30 Q54,36 52,42" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.6" stroke-linecap="round"/>
      <path d="M60,28 Q60,35 60,42" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.6" stroke-linecap="round"/>
      <path d="M68,30 Q66,36 68,42" stroke="#E8923A" stroke-width="2" fill="none" opacity="0.6" stroke-linecap="round"/>

      <!-- Ears -->
      <polygon points="33,38 25,12 48,30" fill="#F4A34D"/>
      <polygon points="87,38 95,12 72,30" fill="#F4A34D"/>
      <!-- Inner ears -->
      <polygon points="35,35 29,16 46,31" fill="#FFB6C1" opacity="0.6"/>
      <polygon points="85,35 91,16 74,31" fill="#FFB6C1" opacity="0.6"/>

      <!-- Eyes -->
      <g class="eyes">
        <!-- Eye whites -->
        <ellipse cx="47" cy="50" rx="9" ry="10" fill="#fff"/>
        <ellipse cx="73" cy="50" rx="9" ry="10" fill="#fff"/>
        <!-- Irises -->
        <ellipse class="iris-l" cx="48" cy="52" rx="6" ry="7" fill="#5B8C5A"/>
        <ellipse class="iris-r" cx="74" cy="52" rx="6" ry="7" fill="#5B8C5A"/>
        <!-- Pupils -->
        <ellipse class="pupil-l" cx="49" cy="53" rx="3" ry="4" fill="#222"/>
        <ellipse class="pupil-r" cx="75" cy="53" rx="3" ry="4" fill="#222"/>
        <!-- Eye highlights -->
        <circle cx="51" cy="49" r="2.5" fill="#fff" opacity="0.9"/>
        <circle cx="77" cy="49" r="2.5" fill="#fff" opacity="0.9"/>
        <circle cx="47" cy="54" r="1.2" fill="#fff" opacity="0.5"/>
        <circle cx="73" cy="54" r="1.2" fill="#fff" opacity="0.5"/>
      </g>

      <!-- Closed eyes (for sleep) -->
      <g class="closed-eyes">
        <path d="M39,52 Q47,46 55,52" stroke="#555" stroke-width="2.5" fill="none" stroke-linecap="round"/>
        <path d="M65,52 Q73,46 81,52" stroke="#555" stroke-width="2.5" fill="none" stroke-linecap="round"/>
      </g>

      <!-- Blush -->
      <ellipse cx="36" cy="60" rx="6" ry="3.5" fill="#FFB6C1" opacity="0.5"/>
      <ellipse cx="84" cy="60" rx="6" ry="3.5" fill="#FFB6C1" opacity="0.5"/>

      <!-- Nose -->
      <ellipse cx="60" cy="58" rx="3.5" ry="2.5" fill="#FF8FA3"/>

      <!-- Mouth -->
      <path d="M55,62 Q60,67 65,62" stroke="#C97755" stroke-width="1.5" fill="none" stroke-linecap="round"/>
      <line x1="60" y1="58.5" x2="60" y2="63" stroke="#C97755" stroke-width="1.5" stroke-linecap="round"/>

      <!-- Whiskers -->
      <g class="whiskers" opacity="0.45">
        <line x1="32" y1="55" x2="14" y2="52" stroke="#8B7355" stroke-width="1.2" stroke-linecap="round"/>
        <line x1="32" y1="59" x2="12" y2="60" stroke="#8B7355" stroke-width="1.2" stroke-linecap="round"/>
        <line x1="32" y1="63" x2="14" y2="67" stroke="#8B7355" stroke-width="1.2" stroke-linecap="round"/>
        <line x1="88" y1="55" x2="106" y2="52" stroke="#8B7355" stroke-width="1.2" stroke-linecap="round"/>
        <line x1="88" y1="59" x2="108" y2="60" stroke="#8B7355" stroke-width="1.2" stroke-linecap="round"/>
        <line x1="88" y1="63" x2="106" y2="67" stroke="#8B7355" stroke-width="1.2" stroke-linecap="round"/>
      </g>

      <!-- Collar -->
      <path d="M38,72 Q60,78 82,72" stroke="#6366F1" stroke-width="3.5" fill="none" stroke-linecap="round"/>
      <!-- Bell -->
      <circle cx="60" cy="77" r="4" fill="#FFD700" stroke="#DAA520" stroke-width="0.8"/>
      <line x1="58" y1="77" x2="62" y2="77" stroke="#DAA520" stroke-width="0.6"/>
      <circle cx="60" cy="79" r="0.8" fill="#DAA520"/>

      <!-- Sleep Zzz -->
      <g class="zzz" opacity="0">
        <text x="85" y="30" font-size="12" font-weight="bold" fill="var(--vp-c-text-3)">Z</text>
        <text x="92" y="22" font-size="9" font-weight="bold" fill="var(--vp-c-text-3)">z</text>
        <text x="97" y="16" font-size="7" font-weight="bold" fill="var(--vp-c-text-3)">z</text>
      </g>
    </svg>

    <!-- Speech bubble -->
    <div v-if="showBubble" class="speech-bubble">{{ bubbleText }}</div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue';

const petRef = ref<HTMLElement>();
const x = ref(40);
const y = ref(20);
const facing = ref(1);
const state = ref<'idle' | 'sleep' | 'jump' | 'walk' | 'drag'>('idle');
const dragging = ref(false);
const showBubble = ref(false);
const bubbleText = ref('');

let dragOffsetX = 0;
let dragOffsetY = 0;
let stateTimer: ReturnType<typeof setTimeout>;
let bubbleTimer: ReturnType<typeof setTimeout>;
let walkTimer: ReturnType<typeof setInterval>;
let walkDirection = 1;

const tooltip = computed(() => {
  switch (state.value) {
    case 'sleep': return '正在打盹...点我叫醒~';
    case 'idle': return '点我互动~';
    case 'walk': return '散步中~';
    default: return '🐱';
  }
});

const greetings = [
  '喵~ 你好呀！',
  '写代码辛苦了~',
  '休息一下吧！',
  '加油！你很棒！',
  '喵呜~ 摸摸我~',
  '今天也要元气满满！',
  'Bug 退散！✨',
  '陪你一起学习喵~',
  '记得喝水哦💧',
];

function showSpeech(text?: string) {
  bubbleText.value = text || greetings[Math.floor(Math.random() * greetings.length)];
  showBubble.value = true;
  clearTimeout(bubbleTimer);
  bubbleTimer = setTimeout(() => { showBubble.value = false; }, 3500);
}

function handleClick() {
  if (dragging.value) return;
  if (state.value === 'sleep') {
    state.value = 'jump';
    showSpeech('哈？谁叫我！😾');
    setTimeout(() => { state.value = 'idle'; scheduleNext(); }, 600);
  } else {
    state.value = 'jump';
    showSpeech();
    setTimeout(() => { state.value = 'idle'; scheduleNext(); }, 600);
  }
}

function scheduleNext() {
  clearTimeout(stateTimer);
  const delay = 6000 + Math.random() * 12000;
  stateTimer = setTimeout(() => {
    const roll = Math.random();
    if (roll < 0.25) {
      state.value = 'sleep';
      scheduleNext();
    } else if (roll < 0.65) {
      startWalk();
    } else {
      state.value = 'idle';
      scheduleNext();
    }
  }, delay);
}

function startWalk() {
  state.value = 'walk';
  walkDirection = Math.random() > 0.5 ? 1 : -1;
  facing.value = walkDirection;
  let steps = 0;
  const maxSteps = 20 + Math.floor(Math.random() * 30);

  clearInterval(walkTimer);
  walkTimer = setInterval(() => {
    const nextX = x.value + walkDirection * 2;
    const maxX = (typeof window !== 'undefined' ? window.innerWidth : 1200) - 100;
    if (nextX < 10 || nextX > maxX) {
      walkDirection *= -1;
      facing.value = walkDirection;
    }
    x.value = Math.max(10, Math.min(nextX, maxX));
    steps++;
    if (steps >= maxSteps) {
      clearInterval(walkTimer);
      state.value = 'idle';
      scheduleNext();
    }
  }, 70);
}

function startDrag(e: MouseEvent | TouchEvent) {
  dragging.value = true;
  state.value = 'drag';
  clearTimeout(stateTimer);
  clearInterval(walkTimer);

  const clientX = 'touches' in e ? e.touches[0].clientX : e.clientX;
  const clientY = 'touches' in e ? e.touches[0].clientY : e.clientY;
  const rect = petRef.value!.getBoundingClientRect();
  dragOffsetX = clientX - rect.left;
  dragOffsetY = rect.bottom - clientY;

  const onMove = (ev: MouseEvent | TouchEvent) => {
    const cx = 'touches' in ev ? ev.touches[0].clientX : ev.clientX;
    const cy = 'touches' in ev ? ev.touches[0].clientY : ev.clientY;
    x.value = Math.max(0, Math.min(cx - dragOffsetX, window.innerWidth - 100));
    y.value = Math.max(0, Math.min(window.innerHeight - cy - dragOffsetY, window.innerHeight - 120));
  };

  const onUp = () => {
    dragging.value = false;
    state.value = 'idle';
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
    document.removeEventListener('touchmove', onMove);
    document.removeEventListener('touchend', onUp);
    showSpeech('放我下来啦~');
    scheduleNext();
  };

  document.addEventListener('mousemove', onMove);
  document.addEventListener('mouseup', onUp);
  document.addEventListener('touchmove', onMove, { passive: false });
  document.addEventListener('touchend', onUp);
}

onMounted(() => {
  x.value = (typeof window !== 'undefined' ? window.innerWidth : 1200) - 140;
  setTimeout(() => showSpeech('喵~ 欢迎光临！'), 2500);
  scheduleNext();
});

onUnmounted(() => {
  clearTimeout(stateTimer);
  clearTimeout(bubbleTimer);
  clearInterval(walkTimer);
});
</script>

<style scoped>
.pixel-pet {
  position: fixed;
  z-index: 999;
  cursor: grab;
  user-select: none;
  transition: bottom 0.3s ease;
}

.pixel-pet.dragging {
  cursor: grabbing;
  transition: none;
}

.cat-svg {
  width: 80px;
  height: 80px;
  filter: drop-shadow(0 3px 6px rgba(0,0,0,0.18));
  overflow: visible;
}

/* === Eye states === */
.closed-eyes { display: none; }
.pixel-pet.sleep .eyes { display: none; }
.pixel-pet.sleep .closed-eyes { display: block; }

/* === Zzz === */
.zzz { opacity: 0; }
.pixel-pet.sleep .zzz {
  opacity: 1;
  animation: zzzFloat 2.5s ease-in-out infinite;
}

/* === Tail wag === */
.tail-group {
  transform-origin: 18px 88px;
  animation: tailSway 2s ease-in-out infinite;
}

/* === Jump === */
.pixel-pet.jump .cat-svg {
  animation: catJump 0.55s cubic-bezier(0.34, 1.56, 0.64, 1);
}

/* === Sleep breathing === */
.pixel-pet.sleep .cat-svg {
  animation: catBreathe 2.5s ease-in-out infinite;
}

/* === Walk bobbing === */
.pixel-pet.walk .cat-svg {
  animation: catWalk 0.35s ease-in-out infinite;
}

.pixel-pet.walk .paw-l { animation: pawLift 0.35s ease-in-out infinite; }
.pixel-pet.walk .paw-r { animation: pawLift 0.35s ease-in-out 0.175s infinite; }

/* === Drag stretch === */
.pixel-pet.drag .cat-svg {
  animation: catStretch 0.3s ease forwards;
}

/* === Whisker twitch === */
.whiskers {
  animation: whiskerTwitch 3s ease-in-out infinite;
}

/* === Speech Bubble === */
.speech-bubble {
  position: absolute;
  bottom: calc(100% + 6px);
  left: 50%;
  transform: translateX(-50%);
  background: var(--vp-c-bg-elv);
  color: var(--vp-c-text-1);
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  padding: 7px 14px;
  font-size: 12px;
  line-height: 1.4;
  white-space: nowrap;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.12);
  animation: bubbleIn 0.35s cubic-bezier(0.34, 1.56, 0.64, 1);
}

.speech-bubble::after {
  content: '';
  position: absolute;
  bottom: -5px;
  left: 50%;
  transform: translateX(-50%) rotate(45deg);
  width: 9px;
  height: 9px;
  background: var(--vp-c-bg-elv);
  border-right: 1px solid var(--vp-c-divider);
  border-bottom: 1px solid var(--vp-c-divider);
}

/* === Animations === */
@keyframes catJump {
  0%   { transform: translateY(0) scaleX(1) scaleY(1); }
  20%  { transform: translateY(2px) scaleX(1.08) scaleY(0.92); }
  45%  { transform: translateY(-22px) scaleX(0.94) scaleY(1.08); }
  70%  { transform: translateY(-24px) scaleX(1) scaleY(1); }
  85%  { transform: translateY(-5px) scaleX(1.03) scaleY(0.97); }
  100% { transform: translateY(0) scaleX(1) scaleY(1); }
}

@keyframes catBreathe {
  0%, 100% { transform: scaleY(1) translateY(0); }
  50%      { transform: scaleY(0.97) translateY(1px); }
}

@keyframes catWalk {
  0%, 100% { transform: translateY(0) rotate(0deg); }
  25%      { transform: translateY(-2px) rotate(-1.5deg); }
  75%      { transform: translateY(-2px) rotate(1.5deg); }
}

@keyframes pawLift {
  0%, 100% { transform: translateY(0); }
  50%      { transform: translateY(-4px); }
}

@keyframes catStretch {
  0%   { transform: scaleY(1) scaleX(1); }
  100% { transform: scaleY(1.12) scaleX(0.92); }
}

@keyframes tailSway {
  0%, 100% { transform: rotate(0deg); }
  30%      { transform: rotate(12deg); }
  70%      { transform: rotate(-8deg); }
}

@keyframes whiskerTwitch {
  0%, 100% { transform: scaleX(1); }
  5%       { transform: scaleX(1.06); }
  10%      { transform: scaleX(1); }
}

@keyframes zzzFloat {
  0%, 100% { transform: translateY(0); opacity: 1; }
  50%      { transform: translateY(-6px); opacity: 0.5; }
}

@keyframes bubbleIn {
  0%   { opacity: 0; transform: translateX(-50%) translateY(8px) scale(0.85); }
  100% { opacity: 1; transform: translateX(-50%) translateY(0) scale(1); }
}

/* === Mobile: slightly smaller === */
@media (max-width: 768px) {
  .cat-svg {
    width: 60px;
    height: 60px;
  }
  .speech-bubble {
    font-size: 11px;
    padding: 5px 10px;
  }
}
</style>
