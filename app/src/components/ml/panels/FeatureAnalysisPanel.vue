<template>
  <div class="feature-analysis-panel">
    <!-- 顶部控制 + 数据收集入口 -->
    <div class="collect-summary">
      <div class="summary-text">
        <span class="section-eyebrow">FEATURE ANALYSIS</span>
        <h3>检查策略上游数据的特征质量</h3>
        <p>从 XGBoost 节点反向遍历子图，收集所有节点的 Vector&lt;double&gt; 输出，统计 NaN 与分布；NaN% &gt; 30% 的特征可能影响训练质量。</p>
      </div>
      <div class="summary-action">
        <button class="btn btn-primary" :disabled="state.featureReport.loading || state.trainResult.loading || !config.labelSource" @click="onCollect">
          <i v-if="state.featureReport.loading" class="fas fa-spinner fa-spin"></i>
          {{ state.featureReport.loading ? '收集中…' : '开始收集' }}
        </button>
        <span v-if="config.labelSource" class="source-chip" :title="`标签来源：${config.labelSource}`">
          <i class="fas fa-tag"></i>{{ config.labelSource }}
        </span>
        <span v-else class="source-chip warn">
          <i class="fas fa-exclamation-circle"></i>未选择标签来源
        </span>
      </div>
    </div>

    <!-- 收集进度 -->
    <div v-if="state.featureReport.steps.length > 0" class="training-progress">
      <div class="progress-header">
        <span class="progress-title">收集进度</span>
        <span v-if="!state.featureReport.loading" class="progress-done">完成</span>
      </div>
      <div class="step-list">
        <div v-for="step in state.featureReport.steps" :key="step.id" class="step-item">
          <span class="step-icon" :class="step.status">
            <template v-if="step.status === 'done'">✓</template>
            <template v-else-if="step.status === 'running'">⏳</template>
            <template v-else-if="step.status === 'error'">✗</template>
            <template v-else>○</template>
          </span>
          <span class="step-label">{{ step.label }}</span>
          <span v-if="step.detail" class="step-detail">{{ step.detail }}</span>
        </div>
      </div>
    </div>

    <!-- 特征统计报告 -->
    <div v-if="state.featureReport.data" class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">FEATURE REPORT</span>
          <h3 class="section-title">特征数据检查</h3>
        </div>
        <span class="section-hint">NaN% &gt; 30% 的特征可能影响训练质量</span>
      </div>
      <FeatureInspectionPanel :report="state.featureReport.data" />
    </div>

    <div v-if="!state.featureReport.data && !state.featureReport.loading" class="empty-state">
      <div class="empty-icon">🔍</div>
      <div>配置标签来源后点击"开始收集"查看特征统计</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useMLData } from '../composables/useMLData'
import type { TrainStep } from '../composables/useMLState'
import FeatureInspectionPanel from './FeatureInspectionPanel.vue'

const props = defineProps<{
  state: any
  script: string
}>()

const { collect } = useMLData()
const config = props.state.config

const STEP_LABELS: Record<string, string> = {
  parse_script: '解析策略图',
  init_nodes: '初始化节点',
  start_exchange: '启动数据源',
  collect_data: '收集特征数据',
  analyze: '分析特征数据',
}

async function onCollect() {
  props.state.featureReport.loading = true
  props.state.featureReport.data = null
  props.state.featureReport.steps = []
  props.state.featureReport.logs = []

  try {
    const report = await collect(props.script, {
      labelSource: config.labelSource,
      startDate: props.state.dateRange.value?.[0] || '',
      endDate: props.state.dateRange.value?.[1] || '',
      frequency: props.state.frequency.value || '1d',
    }, (type: string, data: any) => {
      if (type === 'step') {
        const stepId = data.step
        const existing = props.state.featureReport.steps.find((s: TrainStep) => s.id === stepId)
        if (data.status === 'start') {
          if (!existing) {
            props.state.featureReport.steps.push({
              id: stepId,
              label: STEP_LABELS[stepId] || stepId,
              status: 'running',
              detail: data.msg,
            })
          }
        } else if (data.status === 'done' && existing) {
          existing.status = 'done'
        }
      } else if (type === 'progress') {
        const step = props.state.featureReport.steps.find((s: TrainStep) => s.id === data.step)
        if (step) step.detail = `${data.current} / ${data.total}`
      }
    })
    if (report) {
      props.state.featureReport.data = report
    }
  } catch (err: any) {
    console.error('[ML] collect failed:', err)
  } finally {
    props.state.featureReport.loading = false
  }
}
</script>

<style scoped>
.feature-analysis-panel { padding: 16px 4px 24px; }

.collect-summary {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
  margin-bottom: 18px;
  padding: 16px 20px;
  background: linear-gradient(135deg, rgba(41, 98, 255, 0.18), rgba(41, 98, 255, 0.04) 70%, transparent);
  border: 1px solid rgba(74, 85, 104, 0.35);
  border-radius: 10px;
}
.summary-text { flex: 1; }
.summary-text h3 {
  margin: 4px 0 6px;
  font-size: 17px;
  color: #f1f5f9;
  font-weight: 600;
}
.summary-text p {
  margin: 0;
  color: #94a3b8;
  font-size: 12.5px;
  line-height: 1.6;
  max-width: 640px;
}
.summary-action {
  display: flex;
  gap: 8px;
  align-items: center;
  flex-shrink: 0;
}
.section-eyebrow {
  font-size: 10.5px;
  letter-spacing: 2.2px;
  color: #5b8ff9;
  font-weight: 600;
  text-transform: uppercase;
}

.btn {
  padding: 6px 16px;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  border: none;
  transition: all 0.2s;
}
.btn-primary {
  background: #3b82f6;
  color: white;
}
.btn-primary:hover:not(:disabled) { background: #2563eb; }
.btn-primary:disabled { background: #475569; cursor: not-allowed; opacity: 0.7; }

.source-chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 10px;
  font-size: 11px;
  border-radius: 999px;
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  font-family: 'SF Mono', 'Consolas', monospace;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.source-chip.warn {
  background: rgba(234, 57, 67, 0.12);
  color: #f87171;
}

.training-progress {
  margin-bottom: 16px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 12px 16px;
}
.progress-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 10px;
}
.progress-title { font-size: 13px; font-weight: 600; color: #e2e8f0; }
.progress-done { font-size: 11px; color: #34d399; font-weight: 600; }
.step-list { display: flex; flex-direction: column; gap: 4px; }
.step-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  padding: 3px 0;
}
.step-icon { width: 18px; text-align: center; font-size: 12px; flex-shrink: 0; }
.step-icon.done { color: #34d399; }
.step-icon.running { color: #fbbf24; }
.step-icon.error { color: #f87171; }
.step-icon.pending { color: #64748b; }
.step-label { color: #cbd5e1; white-space: nowrap; }
.step-detail {
  color: #94a3b8;
  font-size: 11px;
  font-family: 'SF Mono', 'Consolas', monospace;
  white-space: nowrap;
}

.chart-section {
  margin-bottom: 18px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 16px;
}
.section-heading {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  margin-bottom: 12px;
}
.section-title {
  color: #e2e8f0;
  font-size: 15px;
  margin: 4px 0 0;
  font-weight: 600;
}
.section-hint { font-size: 11.5px; color: #94a3b8; }

.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }
</style>
