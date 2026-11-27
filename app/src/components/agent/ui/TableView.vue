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
        <tr v-for="(row, i) in filteredVisibleRows" :key="i">
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
    <div v-if="rows.length > 0" class="ui-table-foot">
      <div class="ui-table-foot-left">
        <div class="ui-table-search">
          <select v-model="searchColumn" class="ui-table-search-select">
            <option value="">全部列</option>
            <option v-for="col in columns" :key="col.k" :value="col.k">{{ col.label }}</option>
          </select>
          <input
            v-model="searchText"
            type="text"
            class="ui-table-search-input"
            placeholder="搜索..."
          />
          <button
            v-if="searchText"
            class="ui-table-search-clear"
            @click="searchText = ''"
          >
            <i class="fas fa-times"></i>
          </button>
        </div>
      </div>
      <div class="ui-table-foot-right">
        <div class="ui-table-rows-control">
          <span class="ui-table-rows-label">显示行数</span>
          <input
            v-model.number="displayRows"
            type="range"
            class="ui-table-rows-slider"
            min="1"
            :max="filteredCount"
            step="1"
          />
          <span class="ui-table-rows-value">{{ displayRows }} / {{ filteredCount }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
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

/** 显示行数（响应式，可被滚动条控制） */
const displayRows = ref<number>(rows.value.length)

/** 紧凑态只显示前 5 行；「显示行数」参数优先 */
const maxRows = computed(() => {
  const p = props.params ?? {}
  const limit = p.max_rows as number | undefined
  if (typeof limit === 'number' && limit > 0) return limit
  return props.density === 'compact' ? 5 : displayRows.value
})

const visibleRows = computed(() => rows.value.slice(0, maxRows.value))
const hiddenCount = computed(() => rows.value.length - visibleRows.value.length)

// 搜索相关状态
const searchColumn = ref('')
const searchText = ref('')

/** 根据搜索条件过滤行 */
const filteredRows = computed(() => {
  if (!searchText.value.trim()) return rows.value
  
  const keyword = searchText.value.trim().toLowerCase()
  const colKey = searchColumn.value
  
  return rows.value.filter(row => {
    if (colKey) {
      // 搜索指定列
      const value = row[colKey]
      return matchValue(value, keyword)
    } else {
      // 搜索所有列
      return columns.value.some(col => {
        const value = row[col.k]
        return matchValue(value, keyword)
      })
    }
  })
})

/** 过滤后的可见行（应用 maxRows 限制） */
const filteredVisibleRows = computed(() => {
  return filteredRows.value.slice(0, maxRows.value)
})

/** 过滤后的总行数 */
const filteredCount = computed(() => filteredRows.value.length)

/** 判断单元格值是否匹配搜索词 */
function matchValue(value: unknown, keyword: string): boolean {
  if (value === null || value === undefined || value === '') return false
  const str = String(value).toLowerCase()
  return str.includes(keyword)
}

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

.ui-table-search {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.28);
  background: rgba(255, 255, 255, 0.015);
}

.ui-table-search-select {
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, #2a3449);
  border-radius: 5px;
  color: #e0e0e0;
  padding: 4px 8px;
  font-size: 11px;
  outline: none;
  cursor: pointer;
  font-family: inherit;
  min-width: 80px;
}

.ui-table-search-select option {
  background: var(--panel-bg, #1a2236);
  color: #e0e0e0;
}

.ui-table-search-input {
  flex: 1;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, #2a3449);
  border-radius: 5px;
  color: #e0e0e0;
  padding: 4px 8px;
  font-size: 11px;
  outline: none;
  font-family: inherit;
  min-width: 120px;
}

.ui-table-search-input::placeholder {
  color: #6f7b8a;
}

.ui-table-search-input:focus {
  border-color: #2962ff;
}

.ui-table-search-clear {
  background: transparent;
  border: none;
  color: #6f7b8a;
  cursor: pointer;
  padding: 4px 6px;
  font-size: 11px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.ui-table-search-clear:hover {
  color: #e0e0e0;
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
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 7px 13px;
  font-size: 11px;
  color: #6f7b8a;
  border-top: 1px solid rgba(74, 85, 104, 0.16);
}

.ui-table-foot-left {
  flex: 1;
  display: flex;
  align-items: center;
}

.ui-table-foot-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.ui-table-rows-control {
  display: flex;
  align-items: center;
  gap: 8px;
}

.ui-table-rows-label {
  color: #6f7b8a;
  font-size: 11px;
}

.ui-table-rows-slider {
  width: 100px;
  height: 4px;
  -webkit-appearance: none;
  appearance: none;
  background: rgba(74, 85, 104, 0.3);
  border-radius: 2px;
  outline: none;
  cursor: pointer;
}

.ui-table-rows-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: #2962ff;
  cursor: pointer;
  transition: background 0.2s;
}

.ui-table-rows-slider::-webkit-slider-thumb:hover {
  background: #1e88e5;
}

.ui-table-rows-slider::-moz-range-thumb {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: #2962ff;
  cursor: pointer;
  border: none;
  transition: background 0.2s;
}

.ui-table-rows-slider::-moz-range-thumb:hover {
  background: #1e88e5;
}

.ui-table-rows-value {
  color: #a0aec0;
  font-variant-numeric: tabular-nums;
  min-width: 50px;
  text-align: right;
}
</style>
