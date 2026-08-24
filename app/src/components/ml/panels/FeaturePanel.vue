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
            <button v-for="m in METRICS" :key="m.value"
                    :class="{ active: currentMetric === m.value }"
                    @click="setMetric(m.value)">
              {{ m.label }}
            </button>
          </div>
        </div>
        <FeatureImportanceChart ref="importanceRef" :data="result.feature_importance" :metric="currentMetric" />
      </div>

      <!-- SHAP -->
      <div class="section">
        <div class="section-header">
          <h3 class="section-title">SHAP 特征贡献</h3>
          <button class="btn btn-primary" :disabled="shapLoading || !result" @click="onComputeShap">
            {{ shapLoading ? '计算中…' : (props.state.trainResult.shap ? '重新计算 SHAP' : '计算 SHAP') }}
          </button>
        </div>
        <div v-if="props.state.trainResult.shap" class="shap-info">
          <span class="info-pill">📊 {{ props.state.trainResult.shap.n_samples }} 个测试样本</span>
          <span class="info-pill">📐 {{ props.state.trainResult.shap.features.length }} 个特征</span>
          <span class="info-pill">✅ base_value + ΣSHAP ≈ prediction</span>
          <span v-if="hasDates" class="info-pill">📅 {{ props.state.trainResult.shap.dates?.[0] }} ~ {{ props.state.trainResult.shap.dates?.[props.state.trainResult.shap.dates.length - 1] }}</span>
        </div>

        <!-- SHAP 视图切换 -->
        <div v-if="props.state.trainResult.shap" class="shap-tabs">
          <button :class="{ active: shapView === 'summary' }" @click="shapView = 'summary'">全局摘要</button>
          <button :class="{ active: shapView === 'heatmap' }" @click="shapView = 'heatmap'" :disabled="!hasDates">时间热力图</button>
        </div>

        <!-- 日期过滤（热力图模式） -->
        <div v-if="shapView === 'heatmap' && hasDates" class="date-filter">
          <label>起始</label>
          <input type="date" v-model="shapStartDate" class="date-input" />
          <label>结束</label>
          <input type="date" v-model="shapEndDate" class="date-input" />
          <button class="btn btn-sm" :disabled="shapLoading" @click="onComputeShapRange">查询</button>
        </div>

        <ShapSummaryChart v-if="props.state.trainResult.shap && shapView === 'summary'" :data="props.state.trainResult.shap" />
        <ShapTimeHeatmapChart v-if="props.state.trainResult.shap && shapView === 'heatmap' && heatmapData" :data="heatmapData" />
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import FeatureImportanceChart from '../charts/FeatureImportanceChart.vue'
import ShapSummaryChart from '../charts/ShapSummaryChart.vue'
import ShapTimeHeatmapChart from '../charts/ShapTimeHeatmapChart.vue'
import { useMLData } from '../composables/useMLData'
import type { ShapResult } from '../composables/useMLState'

type Metric = 'gain' | 'weight' | 'cover'

const METRICS: { value: Metric; label: string }[] = [
  { value: 'gain', label: 'Gain · 总增益' },
  { value: 'weight', label: 'Weight · 使用次数' },
  { value: 'cover', label: 'Cover · 覆盖样本' },
]

const props = defineProps<{ state: any }>()
const { shap } = useMLData()

const result = computed(() => props.state.trainResult.data)
const currentMetric = ref<Metric>('gain')
const importanceRef = ref<{ setMetric: (m: Metric) => void } | null>(null)
const shapLoading = ref(false)
const shapView = ref<'summary' | 'heatmap'>('summary')
const shapStartDate = ref('')
const shapEndDate = ref('')

const hasDates = computed(() => {
  const s = props.state.trainResult.shap as ShapResult | undefined
  return s?.dates && s.dates.length > 0
})

const heatmapData = computed(() => {
  // 日期过滤后的结果优先
  return (props.state.trainResult.shapHeatmap || props.state.trainResult.shap) as ShapResult | null
})

function setMetric(m: Metric) {
  currentMetric.value = m
  importanceRef.value?.setMetric(m)
}

async function onComputeShap() {
  if (!result.value) return
  shapLoading.value = true
  try {
    const sh = await shap(result.value.model_id)
    props.state.trainResult.shap = sh
    // 初始化日期范围
    if (sh?.dates?.length) {
      shapStartDate.value = sh.dates[0]
      shapEndDate.value = sh.dates[sh.dates.length - 1]
    }
  } finally {
    shapLoading.value = false
  }
}

async function onComputeShapRange() {
  if (!result.value) return
  shapLoading.value = true
  try {
    const sh = await shap(result.value.model_id, shapStartDate.value || undefined, shapEndDate.value || undefined)
    if (sh) props.state.trainResult.shapHeatmap = sh
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
.btn-sm { padding: 4px 10px; font-size: 11px; }
.shap-info {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 12px;
}
.info-pill {
  background: #2b3a55;
  color: #94a3b8;
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 12px;
}
.shap-tabs {
  display: flex;
  gap: 4px;
  margin-bottom: 12px;
}
.shap-tabs button {
  background: transparent;
  border: 1px solid #2b3a55;
  color: #94a3b8;
  padding: 4px 14px;
  font-size: 12px;
  border-radius: 4px;
  cursor: pointer;
}
.shap-tabs button.active {
  background: #3b82f6;
  color: white;
  border-color: #3b82f6;
}
.shap-tabs button:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.date-filter {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.date-filter label {
  color: #94a3b8;
  font-size: 12px;
}
.date-input {
  background: rgba(0,0,0,0.3);
  border: 1px solid #2b3a55;
  border-radius: 4px;
  color: #e2e8f0;
  padding: 4px 8px;
  font-size: 12px;
  outline: none;
}
.date-input:focus {
  border-color: #3b82f6;
}
.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }
</style>
