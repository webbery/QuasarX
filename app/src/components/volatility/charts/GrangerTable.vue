<template>
  <div class="granger-table">
    <div class="chart-title">格兰杰因果检验</div>
    <div class="table-wrapper">
      <table>
        <thead>
          <tr>
            <th>From</th>
            <th>To</th>
            <th class="sortable" @click="handleSort('f_statistic')">
              F 统计量
              <span class="sort-icon" v-if="sortKey === 'f_statistic'">
                {{ sortOrder === 'asc' ? '↑' : '↓' }}
              </span>
            </th>
            <th class="sortable" @click="handleSort('p_value')">
              p 值
              <span class="sort-icon" v-if="sortKey === 'p_value'">
                {{ sortOrder === 'asc' ? '↑' : '↓' }}
              </span>
            </th>
            <th>最优滞后</th>
            <th class="sortable" @click="handleSort('is_significant')">
              显著性
              <span class="sort-icon" v-if="sortKey === 'is_significant'">
                {{ sortOrder === 'asc' ? '↑' : '↓' }}
              </span>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(item, idx) in sortedData"
            :key="idx"
            :class="{ significant: item.is_significant }"
          >
            <td>{{ item.from }}</td>
            <td>{{ item.to }}</td>
            <td>{{ item.f_statistic.toFixed(4) }}</td>
            <td :class="item.is_significant ? 'text-red' : 'text-gray'">
              {{ item.p_value.toFixed(4) }}
            </td>
            <td>{{ item.optimal_lag }}</td>
            <td>
              <span v-if="item.is_significant" class="badge badge-significant">
                ✗ 拒绝H₀
              </span>
              <span v-else class="badge badge-normal">
                不显著
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'

interface GrangerItem {
  from: string
  to: string
  f_statistic: number
  p_value: number
  is_significant: boolean
  optimal_lag: number
}

const props = defineProps<{
  data: GrangerItem[]
}>()

// 排序状态
const sortKey = ref<'f_statistic' | 'p_value' | 'is_significant' | ''>('')
const sortOrder = ref<'asc' | 'desc'>('asc')

const handleSort = (key: 'f_statistic' | 'p_value' | 'is_significant') => {
  if (sortKey.value === key) {
    sortOrder.value = sortOrder.value === 'asc' ? 'desc' : 'asc'
  } else {
    sortKey.value = key
    sortOrder.value = 'asc'
  }
}

const sortedData = computed(() => {
  const key = sortKey.value
  if (!key) return props.data

  return [...props.data].sort((a, b) => {
    const aVal = a[key]
    const bVal = b[key]

    if (typeof aVal === 'boolean' && typeof bVal === 'boolean') {
      // 布尔值排序：false (不显著) 在前，true (显著) 在后
      const comparison = Number(aVal) - Number(bVal)
      return sortOrder.value === 'asc' ? comparison : -comparison
    }

    const comparison = (aVal as number) - (bVal as number)
    return sortOrder.value === 'asc' ? comparison : -comparison
  })
})
</script>

<style scoped>
.granger-table {
  height: 100%;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: #e0e0e0;
  margin-bottom: 12px;
  padding-left: 8px;
  border-left: 3px solid #2962ff;
}

.table-wrapper {
  flex: 1;
  overflow: auto;
  min-height: 0;
  scrollbar-width: thin;
  scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
}

.table-wrapper::-webkit-scrollbar {
  width: 6px;
}

.table-wrapper::-webkit-scrollbar-track {
  background: transparent;
}

.table-wrapper::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}

.table-wrapper::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
}

table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}

thead th {
  position: sticky;
  top: 0;
  background: rgba(26, 34, 54, 0.95);
  padding: 8px 12px;
  text-align: left;
  color: #999;
  font-weight: 500;
  border-bottom: 2px solid rgba(74, 85, 104, 0.3);
  z-index: 1;
}

thead th.sortable {
  cursor: pointer;
  user-select: none;
  transition: color 0.2s;
}

thead th.sortable:hover {
  color: #e0e0e0;
}

thead th.sortable .sort-icon {
  margin-left: 4px;
  font-size: 10px;
  opacity: 0.8;
}

tbody tr {
  border-bottom: 1px solid rgba(74, 85, 104, 0.15);
  transition: background 0.15s;
}

tbody tr:hover {
  background: rgba(41, 98, 255, 0.05);
}

tbody tr.significant {
  background: rgba(220, 38, 38, 0.08);
}

td {
  padding: 6px 12px;
  color: #e0e0e0;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}

.text-red {
  color: #f87171 !important;
  font-weight: 600;
}

.text-gray {
  color: #666 !important;
}

.badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 3px;
  font-size: 11px;
  font-weight: 500;
}

.badge-significant {
  background: rgba(220, 38, 38, 0.2);
  color: #f87171;
  border: 1px solid rgba(220, 38, 38, 0.3);
}

.badge-normal {
  background: rgba(107, 114, 128, 0.15);
  color: #999;
  border: 1px solid rgba(107, 114, 128, 0.2);
}
</style>
