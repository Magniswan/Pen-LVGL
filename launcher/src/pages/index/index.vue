<template>
  <div class="page">
    <div class="accent"></div>
    <div class="content">
      <div :class="phase === 'error' ? 'status-error' : 'status-ready'">
        <text class="status-symbol">{{ phase === 'error' ? '!' : 'L' }}</text>
      </div>
      <div class="copy">
        <text class="title">{{ title }}</text>
        <text class="detail">{{ detail }}</text>
      </div>
      <div v-if="phase === 'error'" class="retry" @click="retry">
        <text class="retry-text">重试</text>
      </div>
    </div>
    <text class="version">LVGL / 0.1.0</text>
  </div>
</template>

<script>
import { probeLauncher, startLauncher } from '../../services/launcher.js';

export default {
  name: 'LvglLauncherPage',
  data() {
    return {
      phase: 'starting',
      title: '正在启动 LVGL',
      detail: '正在检查设备组件',
      pageActive: false,
      requestActive: false,
      generation: 0,
    };
  },
  methods: {
    onShow() {
      this.pageActive = true;
      this.beginLaunch();
    },
    onHide() {
      this.pageActive = false;
      this.generation += 1;
    },
    onUnload() {
      this.pageActive = false;
      this.generation += 1;
      this.requestActive = false;
    },
    retry() {
      if (this.requestActive) return;
      this.beginLaunch();
    },
    async beginLaunch() {
      if (this.requestActive || !this.pageActive) return;
      this.requestActive = true;
      this.phase = 'starting';
      this.title = '正在启动 LVGL';
      this.detail = '正在检查设备组件';
      const generation = ++this.generation;
      try {
        const probe = await probeLauncher();
        if (!this.pageActive || generation !== this.generation) return;
        if (!probe.available) throw new Error(probe.message || '设备组件不可用');
        this.detail = '正在切换显示与触摸';
        const result = await startLauncher();
        if (!this.pageActive || generation !== this.generation) return;
        if (result.accepted) {
          this.phase = 'launching';
          this.detail = '启动请求已接受';
        } else if (result.state === 'already_running') {
          this.phase = 'launching';
          this.detail = 'LVGL 已在运行';
        } else {
          throw new Error(result.message || '启动请求被拒绝');
        }
      } catch (error) {
        if (!this.pageActive || generation !== this.generation) return;
        this.phase = 'error';
        this.title = '无法启动';
        this.detail = error && error.message ? error.message : '未知错误';
        console.error('[lvgl-launcher] ' + this.detail);
      } finally {
        if (generation === this.generation) this.requestActive = false;
      }
    },
  },
};
</script>

<style lang="less" scoped>
.page {
  width: 960px;
  height: 266px;
  background-color: #17212b;
}

.accent {
  position: absolute;
  left: 0;
  top: 0;
  width: 12px;
  height: 266px;
  background-color: #00a390;
}

.content {
  position: absolute;
  left: 76px;
  top: 65px;
  width: 810px;
  height: 136px;
  flex-direction: row;
  align-items: center;
}

.status-ready,
.status-error {
  width: 92px;
  height: 92px;
  align-items: center;
  justify-content: center;
  border-top-left-radius: 6px;
  border-top-right-radius: 6px;
  border-bottom-left-radius: 6px;
  border-bottom-right-radius: 6px;
}

.status-ready { background-color: #00a390; }
.status-error { background-color: #ef6253; }

.status-symbol {
  color: #ffffff;
  font-size: 46px;
  font-weight: 700;
}

.copy {
  width: 470px;
  height: 92px;
  margin-left: 28px;
  justify-content: center;
}

.title {
  color: #f6f8fa;
  font-size: 28px;
  font-weight: 700;
}

.detail {
  margin-top: 12px;
  color: #aebbc7;
  font-size: 17px;
}

.retry {
  width: 132px;
  height: 52px;
  margin-left: 24px;
  align-items: center;
  justify-content: center;
  background-color: #f6f8fa;
  border-top-left-radius: 6px;
  border-top-right-radius: 6px;
  border-bottom-left-radius: 6px;
  border-bottom-right-radius: 6px;
}

.retry-text {
  color: #17212b;
  font-size: 19px;
  font-weight: 700;
}

.version {
  position: absolute;
  right: 24px;
  bottom: 18px;
  color: #71808d;
  font-size: 13px;
}
</style>
