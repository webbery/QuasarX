<template>
  <div class="agent-table-wrap">
    <table class="agent-table">
      <thead>
        <tr>
          <th
            v-for="col in payload.columns"
            :key="col.key"
            :style="{ textAlign: col.align || (isNumeric(col) ? 'right' : 'left'), width: col.width ? col.width + 'px' : undefined }"
          >
            {{ col.label }}
          </th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(row, i) in visibleRows" :key="i">
          <td
            v-for="col in payload.columns"
            :key="col.key"
            :style="{ textAlign: col.align || (isNumeric(col) ? 'right' : 'left') }"
            :class="{ 'is-numeric': isNumeric(col) }"
          >
            {{ formatCell(row[col.key], col) }}
          </td>
        </tr>
      </tbody>
    </table>
    <div v-if="payload.truncated || hiddenRowCount > 0" class="table-foot">
      <template v-if="payload.truncated">
        仅显示前 {{ payload.rows.length }} 行<template v-if="payload.total">，共 {{ payload.total }} 行</template>
      </template>
      <template v-else-if="hiddenRowCount > 0">
        紧凑模式仅显示前 {{ visibleRows.length }} 行，共 {{ payload.rows.length }} 行
      </template>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ArtifactDensity, TableColumn, TablePayload } from '@/lib/agent/artifacts'

const props = withDefaults(
  defineProps<{
    payload: TablePayload
    /** 呈现密度：紧凑态只显示前 5 行 */
    density?: ArtifactDensity
  }>(),
  { density: 'full' },
)

/** 紧凑态只保留前 5 行 */
const COMPACT_ROWS = 5

const visibleRows = computed(() =>
  props.density === 'compact' ? props.payload.rows.slice(0, COMPACT_ROWS) : props.payload.rows,
)

const hiddenRowCount = computed(() => props.payload.rows.length - visibleRows.value.length)

function isNumeric(col: TableColumn): boolean {
  return col.format === 'number' || col.format === 'percent' || col.format === 'currency' || col.format === 'ratio'
}

function formatCell(value: unknown, col: TableColumn): string {
  if (value === null || value === undefined || value === '') return '—'

  const precision = col.precision ?? (col.format === 'percent' ? 2 : 2)

  if (typeof value === 'number') {
    if (!Number.isFinite(value)) return '—'
    switch (col.format) {
      case 'percent':
        return `${(value * 100).toFixed(precision)}%`
      case 'currency':
        return `¥${value.toLocaleString('zh-CN', { maximumFractionDigits: precision })}`
      case 'ratio':
      case 'number':
        return value.toFixed(precision)
      default:
        return String(value)
    }
  }

  return String(value)
}
</script>

<style scoped>
.agent-table-wrap {
  overflow-x: auto;
}

.agent-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}

.agent-table th {
  text-align: left;
  font-weight: 400;
  color: #6f7b8a;
  font-size: 11px;
  padding: 8px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.28);
  white-space: nowrap;
}

.agent-table td {
  padding: 8px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.16);
  color: #e0e0e0;
}

.agent-table td.is-numeric {
  font-variant-numeric: tabular-nums;
}

.agent-table tr:last-child td {
  border-bottom: none;
}

.table-foot {
  padding: 7px 13px;
  font-size: 11px;
  color: #6f7b8a;
  border-top: 1px solid rgba(74, 85, 104, 0.16);
}
</style>
