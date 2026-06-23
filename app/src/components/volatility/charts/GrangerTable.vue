<template>
  <div class="granger-table">
    <div class="chart-title">格兰杰因果检验</div>
    <div class="table-wrapper">
      <table>
        <thead>
          <tr>
            <th>From</th>
            <th>To</th>
            <th>F 统计量</th>
            <th>p 值</th>
            <th>最优滞后</th>
            <th>显著性</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(item, idx) in data"
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
interface GrangerItem {
  from: string
  to: string
  f_statistic: number
  p_value: number
  is_significant: boolean
  optimal_lag: number
}

defineProps<{
  data: GrangerItem[]
}>()
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
