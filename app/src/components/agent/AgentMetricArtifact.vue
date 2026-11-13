<template>
  <div class="agent-metrics" :style="{ gridTemplateColumns: `repeat(${columns}, minmax(0, 1fr))` }">
    <div v-for="item in payload.items" :key="item.key" class="metric">
      <div class="metric-label">{{ item.label }}</div>
      <div class="metric-value" :class="toneClass(item)">{{ formatValue(item) }}</div>
      <div v-if="item.delta !== undefined || item.deltaLabel" class="metric-delta" :class="deltaClass(item)">
        <span v-if="item.delta !== undefined">
          {{ item.delta > 0 ? '▲' : item.delta < 0 ? '▼' : '—' }}
          {{ formatDelta(item) }}
        </span>
        <span v-if="item.deltaLabel" class="delta-label">{{ item.deltaLabel }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { MetricItem, MetricPayload } from '@/lib/agent/artifacts'

const props = defineProps<{ payload: MetricPayload }>()

/** 每行最多 5 列，指标少时自适应 */
const columns = computed(() => Math.min(Math.max(props.payload.items.length, 1), 5))

function precisionOf(item: MetricItem): number {
  if (item.precision !== undefined) return item.precision
  if (item.format === 'percent') return 1
  if (item.format === 'ratio') return 2
  return 2
}

function formatValue(item: MetricItem): string {
  const raw = item.value
  if (typeof raw === 'string') return raw

  if (!Number.isFinite(raw)) return '—'

  switch (item.format) {
    case 'percent':
      return `${(raw * 100).toFixed(precisionOf(item))}%`
    case 'currency':
      return `¥${raw.toLocaleString('zh-CN', { maximumFractionDigits: precisionOf(item) })}`
    case 'raw':
      return String(raw)
    case 'ratio':
    case 'number':
    default:
      return raw.toFixed(precisionOf(item))
  }
}

function formatDelta(item: MetricItem): string {
  const d = item.delta
  if (d === undefined || !Number.isFinite(d)) return ''
  const abs = Math.abs(d)
  if (item.format === 'percent') return `${(abs * 100).toFixed(precisionOf(item))}%`
  if (item.format === 'currency') return `¥${abs.toLocaleString('zh-CN')}`
  return abs.toFixed(precisionOf(item))
}

function toneClass(item: MetricItem): string {
  if (item.tone) return `is-${item.tone}`
  const raw = item.value
  if (typeof raw !== 'number' || !Number.isFinite(raw)) return ''
  return raw > 0 ? 'is-good' : raw < 0 ? 'is-bad' : ''
}

function deltaClass(item: MetricItem): string {
  if (item.tone) return `is-${item.tone}`
  const d = item.delta
  if (d === undefined) return ''
  return d > 0 ? 'is-good' : d < 0 ? 'is-bad' : ''
}
</script>

<style scoped>
.agent-metrics {
  display: grid;
}

.metric {
  padding: 12px 14px;
  border-right: 1px solid rgba(74, 85, 104, 0.22);
}

.metric:last-child {
  border-right: none;
}

.metric-label {
  font-size: 11px;
  color: #6f7b8a;
  margin-bottom: 4px;
}

.metric-value {
  font-size: 20px;
  font-weight: 500;
  letter-spacing: -0.2px;
  font-variant-numeric: tabular-nums;
  color: #e0e0e0;
}

.metric-value.is-good {
  color: #00c853;
}

.metric-value.is-bad {
  color: #ef5350;
}

.metric-value.is-neutral {
  color: #e0e0e0;
}

.metric-delta {
  font-size: 11px;
  margin-top: 3px;
  color: #6f7b8a;
  display: flex;
  gap: 6px;
  align-items: center;
}

.metric-delta.is-good {
  color: #00c853;
}

.metric-delta.is-bad {
  color: #ef5350;
}

.delta-label {
  color: #6f7b8a;
}
</style>
