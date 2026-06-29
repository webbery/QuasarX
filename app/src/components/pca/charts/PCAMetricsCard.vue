<template>
  <div class="pca-metrics">
    <h4>PCA 质量评估</h4>
    <div class="metrics-grid">
      <div class="metric-card" :class="kmoClass">
        <div class="metric-label">KMO 检验</div>
        <div class="metric-value">{{ quality?.kmo.toFixed(3) ?? '-' }}</div>
        <div class="metric-grade">{{ quality?.kmo_grade ?? '-' }}</div>
      </div>
      <div class="metric-card" :class="bartlettClass">
        <div class="metric-label">Bartlett 球形检验</div>
        <div class="metric-value">χ²={{ quality?.bartlett_stat.toFixed(1) ?? '-' }}</div>
        <div class="metric-grade">
          <template v-if="quality">
            p={{ quality.bartlett_pvalue < 0.001 ? '<0.001' : quality.bartlett_pvalue.toFixed(3) }}
            {{ quality.bartlett_pvalue < 0.05 ? '✓ 显著' : '✗ 不显著' }}
          </template>
        </div>
      </div>
      <div class="metric-card" :class="varianceClass">
        <div class="metric-label">累计方差解释率</div>
        <div class="metric-value">{{ quality ? (quality.cumulative_variance * 100).toFixed(1) + '%' : '-' }}</div>
        <div class="metric-grade">{{ quality?.variance_grade ?? '-' }}</div>
      </div>
      <div class="metric-card" :class="condClass">
        <div class="metric-label">条件数 κ</div>
        <div class="metric-value">{{ quality && quality.condition_number < 1e10 ? quality.condition_number.toFixed(1) : quality ? '∞' : '-' }}</div>
        <div class="metric-grade">{{ quality?.cond_grade ?? '-' }}</div>
      </div>
      <div class="metric-card" :class="reconClass">
        <div class="metric-label">重构误差</div>
        <div class="metric-value">{{ quality?.reconstruction_error.toFixed(4) ?? '-' }}</div>
        <div class="metric-grade">占比 {{ quality ? quality.reconstruction_error_pct.toFixed(1) + '%' : '-' }}</div>
      </div>
      <div class="metric-card" :class="pdClass">
        <div class="metric-label">正定性</div>
        <div class="metric-value">
          <template v-if="quality">
            {{ quality.is_positive_definite ? '✓ 是' : '✗ 否' }}
          </template>
          <template v-else>-</template>
        </div>
        <div class="metric-grade">
          <template v-if="quality">
            {{ quality.is_positive_definite ? '矩阵满秩' : '存在共线性' }}
          </template>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { PCAQualityMetrics } from '../composables/usePCAState'

const props = defineProps<{
  quality: PCAQualityMetrics | undefined
}>()

const kmoClass = computed(() => {
  const v = props.quality?.kmo ?? 0
  return v >= 0.8 ? 'good' : v >= 0.6 ? 'ok' : 'bad'
})

const bartlettClass = computed(() => {
  return (props.quality?.bartlett_pvalue ?? 1) < 0.05 ? 'good' : 'bad'
})

const varianceClass = computed(() => {
  const v = props.quality?.cumulative_variance ?? 0
  return v >= 0.85 ? 'good' : v >= 0.70 ? 'ok' : 'bad'
})

const condClass = computed(() => {
  const v = props.quality?.condition_number ?? 1e15
  return v < 100 ? 'good' : v < 1000 ? 'ok' : v < 10000 ? 'warn' : 'bad'
})

const reconClass = computed(() => {
  const v = props.quality?.reconstruction_error_pct ?? 100
  return v < 10 ? 'good' : v < 20 ? 'ok' : 'bad'
})

const pdClass = computed(() => {
  return props.quality?.is_positive_definite ? 'good' : 'bad'
})
</script>

<style scoped>
.pca-metrics {
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 16px;
}
.pca-metrics h4 {
  margin: 0 0 12px 0;
  color: #e0e0e0;
  font-size: 14px;
}
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
  gap: 12px;
}
.metric-card {
  background: rgba(30, 40, 60, 0.8);
  border-radius: 6px;
  padding: 12px;
  text-align: center;
  border: 1px solid rgba(74, 85, 104, 0.3);
}
.metric-card.good {
  border-color: #00c853;
}
.metric-card.ok {
  border-color: #ff9800;
}
.metric-card.warn {
  border-color: #ff9800;
}
.metric-card.bad {
  border-color: #ef232a;
}
.metric-label {
  color: #999;
  font-size: 11px;
  margin-bottom: 6px;
}
.metric-value {
  color: #e0e0e0;
  font-size: 18px;
  font-weight: bold;
  margin-bottom: 4px;
}
.metric-grade {
  font-size: 11px;
  color: #999;
}
.metric-card.good .metric-grade { color: #00c853; }
.metric-card.ok .metric-grade { color: #ff9800; }
.metric-card.warn .metric-grade { color: #ff9800; }
.metric-card.bad .metric-grade { color: #ef232a; }
</style>
