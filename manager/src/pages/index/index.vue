<template>
  <div class="page">
    <div class="datum-line"></div>
    <div class="identity">
      <text class="eyebrow">LVGL / PLATFORM AUTHORITY</text>
      <text class="title">平台管理器</text>
      <div class="trust-row">
        <div :class="snapshot.available ? 'trust-dot' : 'trust-dot trust-dot-error'"></div>
        <text class="trust-copy">{{ trustSummary }}</text>
      </div>
      <text class="version-copy">{{ versionSummary }}</text>
    </div>

    <div class="operation-rail">
      <div
        v-for="operation in operations"
        :key="operation.id"
        :class="operationClass(operation.id)"
        @click="selectOperation(operation.id)"
      >
        <text class="operation-index">{{ operation.index }}</text>
        <text class="operation-title">{{ operation.title }}</text>
        <text class="operation-detail">{{ operation.detail }}</text>
      </div>
    </div>

    <div v-if="overlayVisible" class="overlay">
      <div class="result-panel">
        <text class="stage-label">{{ stageLabel }}</text>
        <text class="result-title">{{ overlayTitle }}</text>
        <text class="result-detail">{{ overlayDetail }}</text>
        <div class="stage-track">
          <div
            v-for="(stage, index) in stages"
            :key="stage"
            :class="index <= stageIndex ? 'stage-node stage-node-active' : 'stage-node'"
          ></div>
        </div>
        <div v-if="confirmingRemove" class="panel-action action-danger" @click="confirmRemove">
          <text class="panel-action-text">确认仅移除 LVGL 平台</text>
        </div>
        <div v-else-if="!busy" class="panel-action" @click="closeOverlay">
          <text class="panel-action-text">返回</text>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { inspectManager, runManagerOperation } from '../../services/manager.js';
import { operationEnabled } from '../../services/manager-state.js';

const STAGES = ['检查', '验签', '事务', '结果'];

export default {
  name: 'LvglPlatformManagerPage',
  data() {
    return {
      pageActive: false,
      busy: false,
      generation: 0,
      stageIndex: 0,
      stageLabel: '检查',
      overlayTitle: '',
      overlayDetail: '',
      overlayVisible: false,
      confirmingRemove: false,
      stages: STAGES,
      snapshot: {
        success: false,
        available: false,
        payloadAvailable: false,
        installed: false,
        repairRequired: false,
        updateAvailable: false,
        busy: false,
        currentVersion: '',
        payloadVersion: '',
        profileId: '',
        code: 'MANAGER_LOADING',
        detail: '正在读取设备与平台状态',
      },
      operations: [
        { id: 'install', index: '01', title: '安装', detail: '首次部署官方平台' },
        { id: 'repair', index: '02', title: '修复', detail: '重新校验并恢复组件' },
        { id: 'upgrade', index: '03', title: '升级', detail: '切换到更高发布序号' },
        { id: 'remove', index: '04', title: '移除', detail: '保留管理器以便重装' },
      ],
    };
  },
  computed: {
    trustSummary() {
      if (!this.snapshot.available) return this.snapshot.detail || '设备未通过严格预检';
      if (!this.snapshot.payloadAvailable) return '未嵌入官方签名的平台发布包';
      return `官方密钥 · ${this.snapshot.profileId || '等待设备档案'}`;
    },
    versionSummary() {
      if (!this.snapshot.installed) return `未安装 · 内置 ${this.snapshot.payloadVersion || '—'}`;
      const suffix = this.snapshot.updateAvailable ? ' · 可升级' : ' · 已是内置版本';
      return `当前 ${this.snapshot.currentVersion || '未知'}${suffix}`;
    },
  },
  methods: {
    onShow() {
      this.pageActive = true;
      this.refresh();
    },
    onHide() {
      this.pageActive = false;
      this.generation += 1;
      this.confirmingRemove = false;
    },
    onUnload() {
      this.onHide();
    },
    operationClass(operation) {
      const enabled = operationEnabled({ ...this.snapshot, busy: this.busy }, operation);
      const danger = operation === 'remove' ? ' operation-danger' : '';
      return `${enabled ? 'operation' : 'operation operation-disabled'}${danger}`;
    },
    async refresh() {
      const generation = ++this.generation;
      try {
        const result = await inspectManager();
        if (this.pageActive && generation === this.generation) this.snapshot = result;
      } catch (error) {
        if (!this.pageActive || generation !== this.generation) return;
        this.snapshot = {
          ...this.snapshot,
          available: false,
          code: 'MANAGER_INSPECT_FAILED',
          detail: String((error && error.message) || error || '检查失败'),
        };
      }
    },
    selectOperation(operation) {
      if (!operationEnabled({ ...this.snapshot, busy: this.busy }, operation)) return;
      if (operation === 'remove') {
        this.stageIndex = 0;
        this.stageLabel = '需要确认';
        this.overlayTitle = '移除 LVGL 平台？';
        this.overlayDetail = '不会移除本管理器，也不会向 Falcon 或 miniapp 发送终止信号。';
        this.confirmingRemove = true;
        this.overlayVisible = true;
        return;
      }
      this.execute(operation);
    },
    confirmRemove() {
      if (!this.confirmingRemove || this.busy) return;
      this.confirmingRemove = false;
      this.execute('remove');
    },
    async execute(operation) {
      const generation = ++this.generation;
      let transactionTimer = 0;
      this.busy = true;
      this.overlayVisible = true;
      this.confirmingRemove = false;
      this.stageIndex = 0;
      this.stageLabel = STAGES[0];
      this.overlayTitle = '正在执行可信事务';
      this.overlayDetail = '设备身份、目录所有权与兼容性预检';
      try {
        this.stageIndex = 1;
        this.stageLabel = STAGES[1];
        this.overlayDetail = '只验证编译内置的官方 Ed25519 发布密钥';
        transactionTimer = this.$page.setTimeout(() => {
          if (this.pageActive && generation === this.generation) {
            this.stageIndex = 2;
            this.stageLabel = STAGES[2];
            this.overlayDetail = '写入隔离暂存区并原子提交版本与状态';
          }
        }, 350);
        const result = await runManagerOperation(operation);
        if (!this.pageActive || generation !== this.generation) return;
        this.stageIndex = 3;
        this.stageLabel = STAGES[3];
        this.snapshot = result;
        if (!result.success) throw new Error(result.detail || result.code);
        this.overlayTitle = result.code === 'MANAGER_REMOVED' ? '平台已移除' : '可信事务完成';
        this.overlayDetail = result.detail || result.code;
      } catch (error) {
        if (!this.pageActive || generation !== this.generation) return;
        this.stageIndex = 3;
        this.stageLabel = '失败关闭';
        this.overlayTitle = '操作未生效';
        this.overlayDetail = String((error && error.message) || error || '安全检查失败');
      } finally {
        if (transactionTimer) this.$page.clearTimeout(transactionTimer);
        if (generation === this.generation) this.busy = false;
      }
    },
    closeOverlay() {
      if (this.busy) return;
      this.overlayVisible = false;
      this.confirmingRemove = false;
      this.refresh();
    },
  },
};
</script>

<style lang="less" scoped>
.page { width: 960px; height: 266px; position: absolute; left: 0; top: 0; background-color: #f5f7f4; }
.datum-line { position: absolute; left: 0; top: 0; width: 10px; height: 266px; background-color: #d47b45; }
.identity { position: absolute; left: 42px; top: 31px; width: 228px; height: 204px; }
.eyebrow { color: #2f7d68; font-size: 12px; font-weight: 700; }
.title { margin-top: 10px; color: #172033; font-size: 29px; font-weight: 700; }
.trust-row { margin-top: 25px; width: 220px; height: 28px; flex-direction: row; align-items: center; }
.trust-dot { width: 10px; height: 10px; margin-right: 9px; border-radius: 5px; background-color: #2f7d68; }
.trust-dot-error { background-color: #a3473f; }
.trust-copy { width: 198px; color: #294c60; font-size: 13px; lines: 2; text-overflow: ellipsis; }
.version-copy { margin-top: 16px; color: #6f7f86; font-size: 13px; lines: 2; }
.operation-rail { position: absolute; left: 302px; top: 31px; width: 620px; height: 204px; flex-direction: row; flex-wrap: wrap; }
.operation { width: 292px; height: 92px; margin-right: 12px; margin-bottom: 12px; padding-left: 18px; padding-top: 13px; background-color: #dce6e7; border-left-width: 3px; border-left-color: #2f7d68; }
.operation:active { background-color: #cbdadb; }
.operation-danger { border-left-color: #d47b45; }
.operation-disabled { opacity: 0.38; }
.operation-index { color: #2f7d68; font-size: 11px; font-weight: 700; }
.operation-title { margin-top: 3px; color: #172033; font-size: 20px; font-weight: 700; }
.operation-detail { margin-top: 3px; color: #294c60; font-size: 13px; lines: 1; }
.overlay { position: absolute; left: 0; top: 0; width: 960px; height: 266px; align-items: center; justify-content: center; background-color: rgba(23, 32, 51, 0.82); }
.result-panel { width: 650px; height: 204px; padding: 22px 26px; background-color: #f5f7f4; border-top-width: 4px; border-top-color: #d47b45; }
.stage-label { color: #2f7d68; font-size: 12px; font-weight: 700; }
.result-title { margin-top: 9px; color: #172033; font-size: 25px; font-weight: 700; }
.result-detail { margin-top: 9px; width: 590px; color: #294c60; font-size: 14px; lines: 2; text-overflow: ellipsis; }
.stage-track { position: absolute; left: 26px; bottom: 24px; width: 220px; height: 14px; flex-direction: row; align-items: center; }
.stage-node { width: 42px; height: 5px; margin-right: 8px; background-color: #b5c4c7; }
.stage-node-active { background-color: #2f7d68; }
.panel-action { position: absolute; right: 26px; bottom: 16px; width: 170px; height: 42px; align-items: center; justify-content: center; background-color: #294c60; }
.action-danger { width: 220px; background-color: #9b4c42; }
.panel-action-text { color: #f5f7f4; font-size: 14px; font-weight: 700; }
</style>
