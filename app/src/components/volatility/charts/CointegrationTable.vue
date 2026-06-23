<template>
  <div class="cointegration-table">
    <div class="chart-title">协整检验 (Engle-Granger)</div>
    <div class="table-wrapper">
      <table>
        <thead>
          <tr>
            <th>标的 X</th>
            <th>标的 Y</th>
            <th>β 系数</th>
            <th>ADF 统计量</th>
            <th>p 值</th>
            <th>协整</th>
            <th>半衰期</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(item, idx) in data"
            :key="idx"
            :class="{ cointegrated: item.is_cointegrated }"
          >
            <td>{{ item.symbol_x }}</td>
            <td>{{ item.symbol_y }}</td>
            <td>{{ item.beta.toFixed(4) }}</td>
            <td :class="item.is_cointegrated ? 'text-green' : 'text-gray'">
              {{ item.adf_statistic.toFixed(4) }}
            </td>
            <td :class="item.is_cointegrated ? 'text-green' : 'text-gray'">
              {{ item.p_value.toFixed(4) }}
            </td>
            <td>
              <span v-if="item.is_cointegrated" class="badge badge-yes">
                ✓ 是
              </span>
              <span v-else class="badge badge-no">
                ✗ 否
              </span>
            </td>
            <td>
              <template v-if="item.half_life > 0">
                {{ item.half_life.toFixed(1) }} bars
              </template>
              <span v-else class="text-gray">—</span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
interface CointegrationItem {
  symbol_x: string
  symbol_y: string
  beta: number
  alpha: number
  adf_statistic: number
  p_value: number
  is_cointegrated: boolean
  half_life: number
}

defineProps<{
  data: CointegrationItem[]
}>()
</script>

<style scoped>
.cointegration-table {
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

tbody tr.cointegrated {
  background: rgba(34, 197, 94, 0.08);
}

td {
  padding: 6px 12px;
  color: #e0e0e0;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}

.text-green {
  color: #4ade80 !important;
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

.badge-yes {
  background: rgba(34, 197, 94, 0.2);
  color: #4ade80;
  border: 1px solid rgba(34, 197, 94, 0.3);
}

.badge-no {
  background: rgba(107, 114, 128, 0.15);
  color: #999;
  border: 1px solid rgba(107, 114, 128, 0.2);
}
</style>
