<template>
  <div class="ui-table-wrap">
    <table class="ui-table">
      <thead>
        <tr>
          <th
            v-for="col in columns"
            :key="col.k"
            :style="{ textAlign: col.align || (isNumeric(col) ? 'right' : 'left'), width: col.width ? col.width + 'px' : undefined }"
          >
            {{ col.label }}
          </th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(row, i) in visibleRows" :key="i">
          <td
            v-for="col in columns"
            :key="col.k"
            :style="{ textAlign: col.align || (isNumeric(col) ? 'right' : 'left') }"
            :class="{ 'is-numeric': isNumeric(col) }"
          >
            {{ formatCell(row[col.k], col) }}
          </td>
        </tr>
      </tbody>
    </table>
    <div v-if="hiddenCount > 0" class="ui-table-foot">
      仅显示前 {{ visibleRows.length }} 行，共 {{ rows.length }} 行
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ArtifactDensity } from '@/lib/agent/artifacts'
import type { UiTableColumn } from '@/lib/charts/types'

const props = withDefaults(
  defineProps<{
    data: Record<string, unknown>
    params?: Record<string, unknown>
    height?: number
    density?: ArtifactDensity
  }>(),
  { density: 'full' },
)

const columns = computed<UiTableColumn[]>(
  () => (props.data?.columns as UiTableColumn[]) ?? [],
)

const rows = computed<Record<string, unknown>[]>(
  () => (props.data?.rows as Record<string, unknown>[]) ?? [],
)

/** 紧凑态只显示前 5 行；「显示行数」参数优先 */
const maxRows = computed(() => {
  const p = props.params ?? {}
  const limit = p.max_rows as number | undefined
  if (typeof limit === 'number' && limit > 0) return limit
  return props.density === 'compact' ? 5 : rows.value.length
})

const visibleRows = computed(() => rows.value.slice(0, maxRows.value))
const hiddenCount = computed(() => rows.value.length - visibleRows.value.length)

function isNumeric(col: UiTableColumn): boolean {
  return col.fmt === 'number' || col.fmt === 'percent' || col.fmt === 'currency' || col.fmt === 'ratio'
}

function formatCell(value: unknown, col: UiTableColumn): string {
  if (value === null || value === undefined || value === '') return '—'

  const precision = col.precision ?? 2

  if (typeof value === 'number') {
    if (!Number.isFinite(value)) return '—'
    switch (col.fmt) {
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
.ui-table-wrap {
  overflow-x: auto;
}

.ui-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}

.ui-table th {
  text-align: left;
  font-weight: 400;
  color: #6f7b8a;
  font-size: 11px;
  padding: 8px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.28);
  white-space: nowrap;
}

.ui-table td {
  padding: 8px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.16);
  color: #e0e0e0;
}

.ui-table td.is-numeric {
  font-variant-numeric: tabular-nums;
}

.ui-table tr:last-child td {
  border-bottom: none;
}

.ui-table-foot {
  padding: 7px 13px;
  font-size: 11px;
  color: #6f7b8a;
  border-top: 1px solid rgba(74, 85, 104, 0.16);
}
</style>
