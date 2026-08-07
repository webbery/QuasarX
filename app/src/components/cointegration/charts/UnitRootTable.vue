<template>
  <div class="unit-root-table">
    <h4>单位根检验</h4>
    <table>
      <thead>
        <tr>
          <th>标的</th>
          <th>ADF 统计量</th>
          <th>ADF p值</th>
          <th>ADF 结论</th>
          <th>KPSS 统计量</th>
          <th>KPSS p值</th>
          <th>KPSS 结论</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(val, sym) in data" :key="sym">
          <td>{{ sym }}</td>
          <td>{{ val.adf.statistic.toFixed(4) }}</td>
          <td>{{ val.adf.p_value.toFixed(4) }}</td>
          <td>
            <span :class="val.adf.is_stationary ? 'pass' : 'fail'">
              {{ val.adf.is_stationary ? '平稳' : '非平稳' }}
            </span>
          </td>
          <td>{{ val.kpss.statistic.toFixed(4) }}</td>
          <td>{{ val.kpss.p_value.toFixed(4) }}</td>
          <td>
            <span :class="val.kpss.is_stationary ? 'pass' : 'fail'">
              {{ val.kpss.is_stationary ? '平稳' : '非平稳' }}
            </span>
          </td>
        </tr>
      </tbody>
    </table>
    <p class="hint">ADF H₀: 存在单位根 (非平稳)；KPSS H₀: 序列平稳。两者互补验证。</p>
  </div>
</template>

<script setup lang="ts">
import type { UnitRootResult } from '../composables/useCointegrationState'

defineProps<{ data: Record<string, UnitRootResult> }>()
</script>

<style scoped>
.unit-root-table { width: 100%; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td { padding: 6px 10px; border: 1px solid #333; text-align: center; }
th { background: #1a1a2e; color: #aaa; font-weight: 600; }
.pass { color: #4caf50; font-weight: 600; }
.fail { color: #f44336; font-weight: 600; }
.hint { font-size: 12px; color: #888; margin-top: 6px; }
</style>
