<template>
  <div class="feature-panel">
    <div v-if="!result" class="empty-state">
      <div class="empty-icon">🎯</div>
      <div>请先在"训练"面板完成训练</div>
    </div>
    <template v-else>
      <!-- Feature Importance -->
      <div class="section">
        <div class="section-header">
          <h3 class="section-title">特征重要性</h3>
          <div class="metric-toggle">
            <button v-for="m in ['gain', 'weight', 'cover']" :key="m"
                    :class="{ active: currentMetric === m }"
                    @click="currentMetric = m as Metric">
              {{ m }}
            </button>
          </div>
        </div>
        <FeatureImportanceChart :data="result.feature_importance" :metric="currentMetric" />
      </div>

      <!-- SHAP -->
      <div class="section">
        <div class="section-header">
          <h3 class="section-title">SHAP 特征贡献</h3>
          <button class="btn btn-primary" :disabled="shapLoading || !result" @click="onComputeShap">
            {{ shapLoading ? '计算中…' : (state.trainResult.shap ? '重新计算 SHAP' : '计算 SHAP') }}
          </button>
        </div>
        <div v-if="state.trainResult.shap" class="shap-info">
          <span class="info-pill">📊 {{ state.trainResult.shap.n_samples }} 个测试样本</span>
          <span class="info-pill">📐 {{ state.trainResult.shap.features.length }} 个特征</span>
          <span class="info-pill">✅ base_value + ΣSHAP ≈ prediction</span>
        </div>
        <ShapSummaryChart v-if="state.trainResult.shap" :data="state.trainResult.shap" />
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import FeatureImportanceChart from '../charts/FeatureImportanceChart.vue'
import ShapSummaryChart from '../charts/ShapSummaryChart.vue'
import { useXGBoostData } from '../composables/useXGBoostData'

type Metric = 'gain' | 'weight' | 'cover'

const props = defineProps<{ state: any }>()
const { shap } = useXGBoostData()

const result = computed(() => props.state.trainResult.data)
const currentMetric = ref<Metric>('gain')
const shapLoading = ref(false)

async function onComputeShap() {
  if (!result.value) return
  shapLoading.value = true
  try {
    const sh = await shap(result.value.model_id)
    props.state.trainResult.shap = sh
  } finally {
    shapLoading.value = false
  }
}
</script>

<style scoped>
.feature-panel { padding: 16px; }
.section {
  margin-bottom: 24px;
  background: #1e2a3a;
  border-radius: 8px;
  padding: 16px;
}
.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}
.section-title {
  color: #cbd5e1;
  font-size: 14px;
  margin: 0;
  font-weight: 600;
}
.metric-toggle { display: flex; gap: 4px; }
.metric-toggle button {
  background: transparent;
  border: 1px solid #2b3a55;
  color: #94a3b8;
  padding: 4px 12px;
  font-size: 12px;
  border-radius: 4px;
  cursor: pointer;
}
.metric-toggle button.active {
  background: #3b82f6;
  color: white;
  border-color: #3b82f6;
}
.btn {
  background: #3b82f6;
  color: white;
  border: none;
  padding: 6px 16px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
}
.btn:disabled { background: #555; cursor: not-allowed; }
.shap-info {
  display: flex;
  gap: 12px;
  margin-bottom: 12px;
}
.info-pill {
  background: #2b3a55;
  color: #94a3b8;
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 12px;
}
.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }
</style>
