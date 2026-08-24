<template>
  <div class="page" :style="pageStyle">
    <hole v-if="holeVisible" class="session-hole" :style="pageStyle" />
    <div
      v-if="holeVisible"
      class="touch-layer"
      :style="pageStyle"
      @touchstart="onTouchStart"
      @touchmove="onTouchMove"
      @touchend="onTouchEnd"
      @touchcancel="onTouchCancel"
    ></div>

    <div v-if="!holeVisible" class="status-surface" :style="pageStyle">
      <div class="datum-line"></div>
      <div
        :class="phase === 'error' ? 'trust-seal trust-seal-error' : 'trust-seal'"
        :style="trustSealStyle"
      >
        <text class="trust-mark">{{ phase === 'error' ? '!' : 'L' }}</text>
      </div>
      <div class="status-copy" :style="statusCopyStyle">
        <text v-if="logicalHeight >= 120" class="eyebrow">LVGL / VERIFIED SESSION</text>
        <text class="title" :style="titleStyle">{{ title }}</text>
        <text class="detail" :style="detailStyle">{{ detail }}</text>
      </div>
      <div v-if="phase === 'error'" class="retry" :style="retryStyle" @click="retry">
        <text class="retry-text">重新检查</text>
      </div>
      <text v-if="logicalHeight >= 160" class="version">FALCON BRIDGE 0.2.0</text>
    </div>
  </div>
</template>

<script>
import {
  getLauncherStatus,
  probeLauncher,
  sendLauncherTouch,
  startLauncher,
} from '../../services/launcher.js';

const START_POLL_MS = 180;
const READY_POLL_MS = 1000;
const MAX_CONTACTS = 32;

function touchList(value) {
  if (!value || typeof value.length !== 'number') return [];
  const result = [];
  for (let index = 0; index < value.length; index += 1) result.push(value[index]);
  return result;
}

function touchKey(touch, index) {
  const raw = touch && touch.identifier;
  return String(raw === undefined || raw === null ? `fallback-${index}` : raw);
}

export default {
  name: 'LvglHoleLauncherPage',
  data() {
    return {
      phase: 'starting',
      title: '正在建立可信会话',
      detail: '检查平台、设备档案与显示通道',
      pageActive: false,
      requestActive: false,
      holeVisible: false,
      inputReady: false,
      logicalWidth: 960,
      logicalHeight: 266,
      generation: 0,
      pollTimer: 0,
      contactSlots: {},
    };
  },
  computed: {
    pageStyle() {
      return {
        width: `${this.logicalWidth}px`,
        height: `${this.logicalHeight}px`,
      };
    },
    compactLayout() {
      return this.logicalHeight < 180 || this.logicalWidth < 720;
    },
    trustSealStyle() {
      const size = this.compactLayout ? 48 : 84;
      return {
        left: `${this.compactLayout ? 24 : 70}px`,
        top: `${Math.max(8, Math.round((this.logicalHeight - size) / 2))}px`,
        width: `${size}px`,
        height: `${size}px`,
        borderTopLeftRadius: `${Math.round(size / 2)}px`,
        borderTopRightRadius: `${Math.round(size / 2)}px`,
        borderBottomLeftRadius: `${Math.round(size / 2)}px`,
        borderBottomRightRadius: `${Math.round(size / 2)}px`,
      };
    },
    statusCopyStyle() {
      const left = this.compactLayout ? 92 : 188;
      const retrySpace = this.phase === 'error' ? (this.compactLayout ? 152 : 228) : 24;
      return {
        left: `${left}px`,
        top: `${this.compactLayout ? 9 : 58}px`,
        width: `${Math.max(80, this.logicalWidth - left - retrySpace)}px`,
        height: `${Math.max(60, this.logicalHeight - (this.compactLayout ? 18 : 116))}px`,
      };
    },
    retryStyle() {
      const height = this.compactLayout ? 40 : 52;
      return {
        right: `${this.compactLayout ? 16 : 62}px`,
        top: `${Math.max(8, Math.round((this.logicalHeight - height) / 2))}px`,
        width: `${this.compactLayout ? 112 : 142}px`,
        height: `${height}px`,
      };
    },
    titleStyle() {
      return this.compactLayout ? { marginTop: '3px', fontSize: '21px', lines: 1 } : {};
    },
    detailStyle() {
      return this.compactLayout ? { marginTop: '4px', fontSize: '13px', lines: 1 } : {};
    },
  },
  methods: {
    onShow() {
      this.pageActive = true;
      this.beginLaunch();
    },
    onHide() {
      this.cancelTouches();
      this.pageActive = false;
      this.stopPolling();
      this.generation += 1;
      this.holeVisible = false;
      this.inputReady = false;
    },
    onUnload() {
      this.onHide();
      this.requestActive = false;
    },
    retry() {
      if (this.requestActive) return;
      this.beginLaunch();
    },
    stopPolling() {
      if (this.pollTimer) this.$page.clearTimeout(this.pollTimer);
      this.pollTimer = 0;
    },
    applyStatus(status) {
      if (status.logicalWidth >= 240 && status.logicalWidth <= 4096) {
        this.logicalWidth = status.logicalWidth;
      }
      if (status.logicalHeight >= 80 && status.logicalHeight <= 2048) {
        this.logicalHeight = status.logicalHeight;
      }
      this.holeVisible = status.holeReady === true;
      this.inputReady = this.holeVisible && status.inputReady === true;
      if (this.holeVisible) {
        this.phase = 'ready';
        this.title = '可信会话已就绪';
        this.detail = this.inputReady ? '显示与触控通道已连接' : '显示已连接，等待触控通道';
      } else if (status.state === 'error') {
        this.phase = 'error';
        this.title = '会话未通过检查';
        this.detail = status.message || `平台返回错误 ${status.result || 0}`;
      } else {
        this.phase = 'starting';
        this.title = '正在建立可信会话';
        this.detail = status.state === 'starting' || status.state === 'launching'
          ? '等待 LVGL 显示通道进入 hole 区域'
          : '正在读取平台状态';
      }
    },
    schedulePoll(generation, delay) {
      this.stopPolling();
      if (!this.pageActive || generation !== this.generation) return;
      this.pollTimer = this.$page.setTimeout(async () => {
        this.pollTimer = 0;
        if (!this.pageActive || generation !== this.generation) return;
        try {
          const status = await getLauncherStatus();
          if (!this.pageActive || generation !== this.generation) return;
          this.applyStatus(status);
          this.schedulePoll(generation, status.holeReady ? READY_POLL_MS : START_POLL_MS);
        } catch (error) {
          if (!this.pageActive || generation !== this.generation) return;
          this.showError(error);
        }
      }, delay);
    },
    async beginLaunch() {
      if (this.requestActive || !this.pageActive) return;
      this.stopPolling();
      this.requestActive = true;
      this.holeVisible = false;
      this.inputReady = false;
      this.phase = 'starting';
      this.title = '正在建立可信会话';
      this.detail = '检查平台、设备档案与显示通道';
      const generation = ++this.generation;
      try {
        const probe = await probeLauncher();
        if (!this.pageActive || generation !== this.generation) return;
        this.applyStatus(probe);
        if (!probe.available) throw new Error(probe.message || '平台组件或 hole 设备档案不可用');
        if (!probe.holeReady) {
          const started = await startLauncher();
          if (!this.pageActive || generation !== this.generation) return;
          this.applyStatus(started);
          if (!started.accepted && started.state !== 'launching' && started.state !== 'starting'
              && started.state !== 'ready') {
            throw new Error(started.message || 'LVGL 会话拒绝启动');
          }
        }
        this.schedulePoll(generation, this.holeVisible ? READY_POLL_MS : START_POLL_MS);
      } catch (error) {
        if (this.pageActive && generation === this.generation) this.showError(error);
      } finally {
        if (generation === this.generation) this.requestActive = false;
      }
    },
    showError(error) {
      this.cancelTouches();
      this.holeVisible = false;
      this.inputReady = false;
      this.phase = 'error';
      this.title = '无法打开 LVGL';
      this.detail = String((error && error.message) || error || '未知错误');
      console.error(`[lvgl-launcher] ${this.detail}`);
    },
    pointFor(touch) {
      const rawX = Number(touch && (touch.pageX !== undefined ? touch.pageX : touch.screenX));
      const rawY = Number(touch && (touch.pageY !== undefined ? touch.pageY : touch.screenY));
      if (!Number.isFinite(rawX) || !Number.isFinite(rawY)) throw new Error('触控坐标无效');
      return {
        x: Math.max(0, Math.min(this.logicalWidth - 1, Math.round(rawX))),
        y: Math.max(0, Math.min(this.logicalHeight - 1, Math.round(rawY))),
      };
    },
    allocateContact(key) {
      if (this.contactSlots[key] !== undefined) return this.contactSlots[key];
      const occupied = {};
      Object.keys(this.contactSlots).forEach((item) => { occupied[this.contactSlots[item]] = true; });
      for (let slot = 0; slot < MAX_CONTACTS; slot += 1) {
        if (!occupied[slot]) {
          this.$set ? this.$set(this.contactSlots, key, slot) : (this.contactSlots[key] = slot);
          return slot;
        }
      }
      throw new Error('活动触点过多');
    },
    emitTouch(phase, touch, index, release) {
      const key = touchKey(touch, index);
      const existing = this.contactSlots[key];
      const contactId = phase === 'start' ? this.allocateContact(key) : existing;
      if (contactId === undefined) return;
      const point = this.pointFor(touch);
      if (!sendLauncherTouch({ phase, contactId, x: point.x, y: point.y })) {
        throw new Error('触控通道拒绝输入');
      }
      if (release) delete this.contactSlots[key];
    },
    forwardTouches(phase, value, release) {
      if (!this.inputReady) return;
      try {
        touchList(value).forEach((touch, index) => this.emitTouch(phase, touch, index, release));
      } catch (error) {
        this.inputReady = false;
        this.cancelTouches();
        console.error(`[lvgl-launcher] ${String(error && error.message ? error.message : error)}`);
      }
    },
    onTouchStart(event) {
      this.forwardTouches('start', event && (event.changedTouches || event.touches), false);
    },
    onTouchMove(event) {
      this.forwardTouches('move', event && event.touches, false);
    },
    onTouchEnd(event) {
      this.forwardTouches('end', event && event.changedTouches, true);
    },
    onTouchCancel() {
      this.cancelTouches();
    },
    cancelTouches() {
      const contacts = this.contactSlots;
      this.contactSlots = {};
      Object.keys(contacts).forEach((key) => {
        try {
          sendLauncherTouch({ phase: 'cancel', contactId: contacts[key], x: 0, y: 0 });
        } catch (_) {}
      });
    },
  },
};
</script>

<style lang="less" scoped>
.page,
.status-surface {
  position: absolute;
  left: 0;
  top: 0;
  background-color: #f5f7f4;
}

.session-hole,
.touch-layer {
  position: absolute;
  left: 0;
  top: 0;
}

.touch-layer { background-color: transparent; }

.datum-line {
  position: absolute;
  left: 0;
  top: 0;
  width: 10px;
  height: 100%;
  background-color: #d47b45;
}

.trust-seal {
  position: absolute;
  left: 70px;
  top: 76px;
  width: 84px;
  height: 84px;
  align-items: center;
  justify-content: center;
  background-color: #2f7d68;
  border-top-left-radius: 42px;
  border-top-right-radius: 42px;
  border-bottom-left-radius: 42px;
  border-bottom-right-radius: 42px;
}

.trust-seal-error { background-color: #a3473f; }

.trust-mark {
  color: #f5f7f4;
  font-size: 38px;
  font-weight: 700;
}

.status-copy {
  position: absolute;
  left: 188px;
  top: 58px;
  width: 520px;
  height: 150px;
}

.eyebrow {
  color: #2f7d68;
  font-size: 13px;
  font-weight: 700;
}

.title {
  margin-top: 12px;
  color: #172033;
  font-size: 29px;
  font-weight: 700;
}

.detail {
  margin-top: 13px;
  color: #294c60;
  font-size: 16px;
}

.retry {
  position: absolute;
  right: 62px;
  top: 101px;
  width: 142px;
  height: 52px;
  align-items: center;
  justify-content: center;
  background-color: #294c60;
}

.retry-text {
  color: #f5f7f4;
  font-size: 17px;
  font-weight: 700;
}

.version {
  position: absolute;
  right: 24px;
  bottom: 16px;
  color: #6f7f86;
  font-size: 12px;
}
</style>
