<!-- app/src/components/report/ReportConfigPanel.vue -->
<!-- 报表配置面板 - 右侧抽屉式配置界面 -->

<template>
  <div class="config-panel">
    <div class="config-content">
      <div class="config-header">
        <h3>📊 图表配置</h3>
        <p class="config-desc">选择要在报表中显示的图表</p>
      </div>

      <!-- 快捷操作 -->
      <div class="quick-actions">
        <button class="action-btn" @click="setAllVisible(true)">
          <span class="icon">✓</span> 全选
        </button>
        <button class="action-btn" @click="setAllVisible(false)">
          <span class="icon">✕</span> 取消全选
        </button>
        <button class="action-btn reset" @click="confirmReset">
          <span class="icon">↺</span> 重置默认
        </button>
      </div>

      <!-- 图表列表 -->
      <div class="chart-list">
        <div
          v-for="chart in sortedCharts"
          :key="chart.id"
          class="chart-item"
          :class="{ disabled: !chart.visible }"
        >
          <label class="chart-item-label">
            <input
              type="checkbox"
              :checked="chart.visible"
              @change="toggleChart(chart.id)"
            />
            <span class="chart-icon">{{ chart.icon }}</span>
            <div class="chart-info">
              <span class="chart-name">{{ chart.label }}</span>
              <span class="chart-desc" v-if="chart.description">{{ chart.description }}</span>
            </div>
          </label>
        </div>
      </div>

      <!-- 其他配置 -->
      <div class="other-config">
        <h4>其他设置</h4>
        <label class="config-option">
          <input
            type="checkbox"
            v-model="localShowMetricsTable"
            @change="updateMetricsTableSetting"
          />
          <span>显示指标表格</span>
        </label>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { CHART_REGISTRY, type ChartDefinition } from './config/chartRegistry'

interface Props {
  /** 图表可见性配置 */
  chartVisibility: Record<string, boolean>
  /** 是否显示指标表格 */
  showMetricsTable: boolean
}

interface Emits {
  (e: 'update:chartVisibility', value: Record<string, boolean>): void
  (e: 'update:showMetricsTable', value: boolean): void
  (e: 'reset'): void
}

const props = defineProps<Props>()
const emit = defineEmits<Emits>()

const localShowMetricsTable = ref(props.showMetricsTable)

// 排序后的图表列表
const sortedCharts = computed(() => {
  return CHART_REGISTRY
    .map(chart => ({
      ...chart,
      visible: props.chartVisibility[chart.id] ?? chart.defaultVisible
    }))
    .sort((a, b) => a.defaultOrder - b.defaultOrder)
})

function toggleChart(chartId: string) {
  const newVisibility = {
    ...props.chartVisibility,
    [chartId]: !props.chartVisibility[chartId]
  }
  emit('update:chartVisibility', newVisibility)
}

function setAllVisible(visible: boolean) {
  const newVisibility: Record<string, boolean> = {}
  CHART_REGISTRY.forEach(chart => {
    newVisibility[chart.id] = visible
  })
  emit('update:chartVisibility', newVisibility)
}

function confirmReset() {
  if (confirm('确定要重置为默认配置吗？')) {
    emit('reset')
  }
}

function updateMetricsTableSetting() {
  emit('update:showMetricsTable', localShowMetricsTable.value)
}

// 监听 props 变化同步到本地
watch(() => props.showMetricsTable, (val) => {
  localShowMetricsTable.value = val
})
</script>

<style scoped>
.config-panel {
  width: 100%;
  height: 100%;
  background: var(--panel-bg, #1a2236);
  display: flex;
  flex-direction: column;
}

.config-content {
  flex: 1;
  overflow-y: auto;
  padding: 20px;
}

.config-header {
  margin-bottom: 20px;
  padding-bottom: 15px;
  border-bottom: 1px solid var(--border, #2a3449);
}

.config-header h3 {
  margin: 0 0 8px 0;
  font-size: 18px;
  color: var(--text, #e0e0e0);
}

.config-desc {
  margin: 0;
  font-size: 13px;
  color: var(--text-secondary, #a0aec0);
}

.quick-actions {
  display: flex;
  gap: 8px;
  margin-bottom: 20px;
}

.action-btn {
  flex: 1;
  padding: 8px 12px;
  background: rgba(42, 52, 77, 0.5);
  border: 1px solid var(--border, #2a3449);
  border-radius: 6px;
  color: var(--text, #e0e0e0);
  font-size: 13px;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
}

.action-btn:hover {
  background: rgba(41, 98, 255, 0.2);
  border-color: #2962ff;
}

.action-btn.reset {
  color: #ff6d00;
}

.action-btn.reset:hover {
  background: rgba(255, 109, 0, 0.2);
  border-color: #ff6d00;
}

.chart-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 24px;
}

.chart-item {
  background: rgba(42, 52, 77, 0.3);
  border: 1px solid var(--border, #2a3449);
  border-radius: 8px;
  padding: 12px;
  transition: all 0.2s;
}

.chart-item:hover {
  border-color: #2962ff;
  background: rgba(41, 98, 255, 0.1);
}

.chart-item.disabled {
  opacity: 0.5;
}

.chart-item-label {
  display: flex;
  align-items: center;
  gap: 10px;
  cursor: pointer;
  user-select: none;
}

.chart-item-label input[type="checkbox"] {
  width: 18px;
  height: 18px;
  cursor: pointer;
  accent-color: #2962ff;
}

.chart-icon {
  font-size: 20px;
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(41, 98, 255, 0.1);
  border-radius: 6px;
  flex-shrink: 0;
}

.chart-info {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.chart-name {
  font-size: 14px;
  font-weight: 500;
  color: var(--text, #e0e0e0);
}

.chart-desc {
  font-size: 12px;
  color: var(--text-secondary, #a0aec0);
}

.other-config {
  padding-top: 16px;
  border-top: 1px solid var(--border, #2a3449);
}

.other-config h4 {
  margin: 0 0 12px 0;
  font-size: 14px;
  color: var(--text, #e0e0e0);
}

.config-option {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px;
  background: rgba(42, 52, 77, 0.3);
  border-radius: 6px;
  cursor: pointer;
}

.config-option input[type="checkbox"] {
  width: 18px;
  height: 18px;
  cursor: pointer;
  accent-color: #2962ff;
}

.config-option span {
  font-size: 14px;
  color: var(--text, #e0e0e0);
}

/* 滚动条样式 */
.config-content::-webkit-scrollbar {
  width: 6px;
}

.config-content::-webkit-scrollbar-track {
  background: rgba(42, 52, 77, 0.3);
}

.config-content::-webkit-scrollbar-thumb {
  background: rgba(41, 98, 255, 0.5);
  border-radius: 3px;
}

.config-content::-webkit-scrollbar-thumb:hover {
  background: rgba(41, 98, 255, 0.7);
}
</style>
