<template>
  <div class="johansen-table">
    <h4>Johansen 多元协整检验</h4>
    <div class="sub-section">
      <h5>Trace 检验</h5>
      <p class="hint">H₀: 协整秩 ≤ r vs H₁: 协整秩 > r</p>
      <table>
        <thead>
          <tr>
            <th>H₀ (r)</th>
            <th>Trace 统计量</th>
            <th>95% 临界值</th>
            <th>99% 临界值</th>
            <th>结论</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(_, r) in data.trace_stats" :key="'trace-'+r"
              :class="{ significant: data.trace_significant[r] }">
            <td>r = {{ r }}</td>
            <td>{{ data.trace_stats[r].toFixed(3) }}</td>
            <td>{{ data.trace_cv_95[r].toFixed(3) }}</td>
            <td>{{ data.trace_cv_99[r].toFixed(3) }}</td>
            <td>
              <span :class="data.trace_significant[r] ? 'reject' : 'accept'">
                {{ data.trace_significant[r] ? '拒绝 H₀' : '不拒绝' }}
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="sub-section">
      <h5>Max-Eigen 检验</h5>
      <p class="hint">H₀: 协整秩 = r vs H₁: 协整秩 = r+1</p>
      <table>
        <thead>
          <tr>
            <th>H₀ (r)</th>
            <th>Max-Eigen 统计量</th>
            <th>95% 临界值</th>
            <th>99% 临界值</th>
            <th>结论</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(_, r) in data.max_eigen_stats" :key="'me-'+r"
              :class="{ significant: data.max_eigen_significant[r] }">
            <td>r = {{ r }}</td>
            <td>{{ data.max_eigen_stats[r].toFixed(3) }}</td>
            <td>{{ data.max_eigen_cv_95[r].toFixed(3) }}</td>
            <td>{{ data.max_eigen_cv_99[r].toFixed(3) }}</td>
            <td>
              <span :class="data.max_eigen_significant[r] ? 'reject' : 'accept'">
                {{ data.max_eigen_significant[r] ? '拒绝 H₀' : '不拒绝' }}
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="summary">
      估计协整秩 (5%): <strong>{{ data.rank }}</strong> / {{ data.n_variables }}
    </div>
  </div>
</template>

<script setup lang="ts">
import type { JohansenResult } from '../composables/useCointegrationState'

defineProps<{ data: JohansenResult }>()
</script>

<style scoped>
.johansen-table { width: 100%; }
.sub-section { margin-bottom: 16px; }
h4 { margin-bottom: 12px; color: #ccc; }
h5 { color: #aaa; font-size: 13px; margin-bottom: 4px; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td { padding: 6px 10px; border: 1px solid #333; text-align: center; }
th { background: #1a1a2e; color: #aaa; font-weight: 600; }
tr.significant { background: rgba(76, 175, 80, 0.08); }
.reject { color: #f44336; font-weight: 600; }
.accept { color: #888; }
.hint { font-size: 11px; color: #666; margin-bottom: 4px; }
.summary { margin-top: 12px; color: #ccc; font-size: 14px; }
</style>
