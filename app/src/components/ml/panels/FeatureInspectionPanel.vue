<template>
  <div class="feature-inspection">
    <!-- 汇总信息 -->
    <div v-if="report" class="summary-bar">
      <div class="summary-item">
        <span class="summary-label">总行数</span>
        <span class="summary-value">{{ report.total_rows }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">特征数</span>
        <span class="summary-value">{{ report.n_features }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">日期范围</span>
        <span class="summary-value">{{ report.date_start }} ~ {{ report.date_end }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">问题特征</span>
        <span class="summary-value" :class="{ 'warn': problemCount > 0 }">
          {{ problemCount }} 个 NaN&gt;30%
        </span>
      </div>
    </div>

    <!-- 特征表格 -->
    <div v-if="report" class="feature-table-wrapper">
      <table class="feature-table">
        <thead>
          <tr>
            <th>特征名</th>
            <th>有效值</th>
            <th>NaN%</th>
            <th>最小值</th>
            <th>最大值</th>
            <th>均值</th>
            <th>标准差</th>
            <th>中位数</th>
            <th>状态</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="f in sortedFeatures" :key="f.name" :class="rowClass(f)">
            <td class="feature-name" :title="f.name">{{ f.name }}</td>
            <td>{{ f.valid }}/{{ f.valid + f.nan_count }}</td>
            <td :class="nanClass(f.nan_pct)">{{ f.nan_pct.toFixed(1) }}%</td>
            <td>{{ fmtNum(f.min) }}</td>
            <td>{{ fmtNum(f.max) }}</td>
            <td>{{ fmtNum(f.mean) }}</td>
            <td>{{ fmtNum(f.std) }}</td>
            <td>{{ fmtNum(f.median) }}</td>
            <td>{{ statusIcon(f) }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 操作按钮 -->
    <div v-if="report" class="action-bar">
      <button class="btn-primary" @click="$emit('startTrain', report.csv_path)">
        开始训练
      </button>
      <span class="hint">使用已收集的特征数据，跳过数据收集阶段</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { FeatureReport, FeatureStat } from '../composables/useMLState'

const props = defineProps<{
  report: FeatureReport | null
}>()

defineEmits<{
  startTrain: [csvPath: string]
}>()

const sortedFeatures = computed(() => {
  if (!props.report?.features) return []
  return [...props.report.features].sort((a, b) => b.nan_pct - a.nan_pct)
})

const problemCount = computed(() => {
  if (!props.report?.features) return 0
  return props.report.features.filter(f => f.nan_pct > 30).length
})

function fmtNum(v: number | null): string {
  if (v == null) return '—'
  if (Math.abs(v) >= 1000) return v.toFixed(1)
  if (Math.abs(v) >= 1) return v.toFixed(3)
  if (v === 0) return '0'
  return v.toExponential(2)
}

function nanClass(pct: number): string {
  if (pct > 30) return 'nan-high'
  if (pct > 10) return 'nan-mid'
  return ''
}

function rowClass(f: FeatureStat): string {
  if (f.nan_pct > 30) return 'row-warn'
  if (f.nan_pct > 10) return 'row-caution'
  return ''
}

function statusIcon(f: FeatureStat): string {
  if (f.nan_pct > 50) return '❌'
  if (f.nan_pct > 30) return '⚠️'
  if (f.nan_pct > 10) return '🟡'
  return '✅'
}
</script>

<style scoped>
.feature-inspection {
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.summary-bar {
  display: flex;
  gap: 24px;
  padding: 10px 16px;
  background: rgba(15, 25, 41, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
}
.summary-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.summary-label {
  font-size: 11px;
  color: #94a3b8;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.summary-value {
  font-size: 14px;
  font-weight: 600;
  color: #e2e8f0;
}
.summary-value.warn { color: #f59e0b; }

.feature-table-wrapper {
  overflow-x: auto;
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
}
.feature-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}
.feature-table th {
  padding: 8px 10px;
  text-align: left;
  font-weight: 600;
  color: #94a3b8;
  background: rgba(15, 25, 41, 0.6);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  white-space: nowrap;
}
.feature-table td {
  padding: 6px 10px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.15);
  color: #cbd5e1;
  white-space: nowrap;
}
.feature-table tr:hover { background: rgba(59, 130, 246, 0.05); }
.feature-name {
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  font-family: monospace;
  font-size: 11px;
}
.row-warn { background: rgba(239, 68, 68, 0.06); }
.row-caution { background: rgba(245, 158, 11, 0.06); }
.nan-high { color: #ef4444; font-weight: 600; }
.nan-mid { color: #f59e0b; }

.action-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding-top: 4px;
}
.btn-primary {
  padding: 8px 20px;
  background: #3b82f6;
  color: #fff;
  border: none;
  border-radius: 6px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.15s;
}
.btn-primary:hover { background: #2563eb; }
.hint {
  font-size: 12px;
  color: #64748b;
}
</style>
