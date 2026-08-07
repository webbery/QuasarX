<template>
  <div class="granger-table">
    <h4>Granger 因果检验</h4>

    <div class="sub-section" v-if="data.pairwise?.length">
      <h5>二元 Granger (F 检验)</h5>
      <table>
        <thead>
          <tr>
            <th>方向</th>
            <th>F 统计量</th>
            <th>p 值</th>
            <th>最优滞后</th>
            <th>结论</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(row, idx) in data.pairwise" :key="'p-'+idx"
              :class="{ significant: row.is_significant }">
            <td>{{ row.from }}</td>
            <td>{{ (row.f_statistic ?? 0).toFixed(3) }}</td>
            <td>{{ row.p_value.toFixed(4) }}</td>
            <td>{{ row.optimal_lag }}</td>
            <td>
              <span :class="row.is_significant ? 'reject' : 'accept'">
                {{ row.is_significant ? '拒绝 H₀' : '不拒绝' }}
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="sub-section" v-if="data.multivariate?.length">
      <h5>多元 Granger (Wald 检验, χ²)</h5>
      <p class="hint">控制其他变量后, X 是否 Granger-cause Y</p>
      <table>
        <thead>
          <tr>
            <th>From</th>
            <th>To</th>
            <th>Wald 统计量</th>
            <th>p 值</th>
            <th>最优滞后</th>
            <th>条件集</th>
            <th>结论</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(row, idx) in data.multivariate" :key="'m-'+idx"
              :class="{ significant: row.is_significant }">
            <td>{{ row.from }}</td>
            <td>{{ row.to }}</td>
            <td>{{ (row.wald_stat ?? 0).toFixed(3) }}</td>
            <td>{{ row.p_value.toFixed(4) }}</td>
            <td>{{ row.optimal_lag }}</td>
            <td class="cond-set">{{ row.condition_set?.join(', ') || '—' }}</td>
            <td>
              <span :class="row.is_significant ? 'reject' : 'accept'">
                {{ row.is_significant ? '拒绝 H₀' : '不拒绝' }}
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { GrangerPairResult } from '../composables/useCointegrationState'

defineProps<{
  data: {
    pairwise: GrangerPairResult[]
    multivariate?: GrangerPairResult[]
  }
}>()
</script>

<style scoped>
.granger-table { width: 100%; }
.sub-section { margin-bottom: 16px; }
h4 { margin-bottom: 12px; color: #ccc; }
h5 { color: #aaa; font-size: 13px; margin-bottom: 4px; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td { padding: 6px 10px; border: 1px solid #333; text-align: center; }
th { background: #1a1a2e; color: #aaa; font-weight: 600; }
tr.significant { background: rgba(244, 67, 54, 0.08); }
.reject { color: #f44336; font-weight: 600; }
.accept { color: #888; }
.cond-set { font-size: 11px; color: #999; max-width: 120px; overflow: hidden; text-overflow: ellipsis; }
.hint { font-size: 11px; color: #666; margin-bottom: 4px; }
</style>
