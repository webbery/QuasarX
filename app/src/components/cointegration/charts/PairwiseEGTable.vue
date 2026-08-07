<template>
  <div class="pairwise-eg-table">
    <h4>Engle-Granger 二元协整</h4>
    <table>
      <thead>
        <tr>
          <th>标的X</th>
          <th>标的Y</th>
          <th>β (协整系数)</th>
          <th>R²</th>
          <th>ADF 统计量</th>
          <th>ADF p值</th>
          <th>KPSS 统计量</th>
          <th>半衰期</th>
          <th>协整</th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="(row, idx) in data"
          :key="idx"
          :class="{ cointegrated: row.is_cointegrated }"
          @click="$emit('select', idx)"
        >
          <td>{{ row.symbol_x }}</td>
          <td>{{ row.symbol_y }}</td>
          <td>{{ row.beta.toFixed(4) }}</td>
          <td>{{ row.r_squared.toFixed(4) }}</td>
          <td>{{ row.adf.statistic.toFixed(4) }}</td>
          <td>{{ row.adf.p_value.toFixed(4) }}</td>
          <td>{{ row.kpss.statistic.toFixed(4) }}</td>
          <td>{{ row.half_life > 0 ? row.half_life.toFixed(1) : '—' }}</td>
          <td>
            <span :class="row.is_cointegrated ? 'pass' : 'neutral'">
              {{ row.is_cointegrated ? '✓ 是' : '否' }}
            </span>
          </td>
        </tr>
      </tbody>
    </table>
    <p class="hint">
      Step 1: OLS 回归 y = α + βx + ε，检验 β 显著性。<br>
      Step 2: 对残差 ε 做 ADF 检验 (MacKinnon 临界值)，H₀: 存在单位根 (非协整)。
    </p>
  </div>
</template>

<script setup lang="ts">
import type { EGFullResult } from '../composables/useCointegrationState'

defineProps<{ data: EGFullResult[] }>()
defineEmits<{ select: [idx: number] }>()
</script>

<style scoped>
.pairwise-eg-table { width: 100%; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td { padding: 6px 10px; border: 1px solid #333; text-align: center; }
th { background: #1a1a2e; color: #aaa; font-weight: 600; }
tr { cursor: pointer; transition: background 0.15s; }
tr:hover { background: #1a1a3e; }
tr.cointegrated { background: rgba(76, 175, 80, 0.08); }
.pass { color: #4caf50; font-weight: 600; }
.neutral { color: #888; }
.hint { font-size: 12px; color: #888; margin-top: 6px; line-height: 1.6; }
</style>
