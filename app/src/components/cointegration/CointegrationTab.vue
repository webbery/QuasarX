<template>
  <div class="cointegration-tab">
    <!-- 配置区 -->
    <div class="config-section">
      <div class="config-row">
        <label>标的 (逗号分隔)</label>
        <input v-model="symbolsInput" placeholder="sz.000001,sz.000002,sz.000003" class="symbols-input" />
      </div>
      <div class="config-row">
        <label>最大滞后</label>
        <input v-model.number="state.maxLag.value" type="number" min="1" max="40" class="num-input" />
        <button class="analyze-btn" @click="onAnalyze" :disabled="state.loading.value">
          {{ state.loading.value ? '分析中...' : '分析' }}
        </button>
      </div>
      <div v-if="state.error.value" class="error-msg">{{ state.error.value }}</div>
    </div>

    <!-- 结果区 -->
    <div v-if="state.result.value" class="results-section">
      <!-- 1. 单位根检验 -->
      <div class="result-block">
        <UnitRootTable :data="state.result.value.unit_root" />
      </div>

      <!-- 2. 二元协整 -->
      <div class="result-block">
        <PairwiseEGTable :data="state.result.value.pairwise_eg" @select="onSelectEG" />
      </div>

      <!-- 3. 残差图 + OU 拟合 (选中某对时展示) -->
      <div v-if="selectedEG" class="result-block charts-row">
        <ResidualChart :data="selectedEG" :height="350" />
        <OUProcessChart :ou="selectedEG.ou" :residuals="selectedEG.residuals" :height="350" />
      </div>

      <!-- 4. Johansen (≥3 标的) -->
      <div v-if="state.hasJohansen.value" class="result-block">
        <JohansenTable :data="state.result.value.johansen!" />
      </div>

      <!-- 5. Granger 因果 -->
      <div class="result-block">
        <GrangerTable :data="state.result.value.granger" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useCointegrationState } from './composables/useCointegrationState'
import UnitRootTable from './charts/UnitRootTable.vue'
import PairwiseEGTable from './charts/PairwiseEGTable.vue'
import ResidualChart from './charts/ResidualChart.vue'
import OUProcessChart from './charts/OUProcessChart.vue'
import JohansenTable from './charts/JohansenTable.vue'
import GrangerTable from './charts/GrangerTable.vue'

const state = useCointegrationState()
const symbolsInput = ref('')
const selectedEGIndex = ref(-1)

const selectedEG = computed(() => {
  if (selectedEGIndex.value < 0 || !state.result.value) return null
  return state.result.value.pairwise_eg[selectedEGIndex.value] ?? null
})

function onAnalyze() {
  state.symbols.value = symbolsInput.value.split(',').map(s => s.trim()).filter(Boolean)
  selectedEGIndex.value = -1
  state.analyze()
}

function onSelectEG(idx: number) {
  selectedEGIndex.value = idx
}
</script>

<style scoped>
.cointegration-tab {
  padding: 12px;
  overflow-y: auto;
  height: 100%;
}

.config-section {
  margin-bottom: 16px;
  padding: 12px;
  background: #111;
  border-radius: 6px;
}

.config-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.config-row label {
  font-size: 12px;
  color: #aaa;
  min-width: 60px;
}

.symbols-input {
  flex: 1;
  background: #1a1a2e;
  border: 1px solid #333;
  border-radius: 4px;
  padding: 6px 10px;
  color: #ddd;
  font-size: 13px;
}

.num-input {
  width: 60px;
  background: #1a1a2e;
  border: 1px solid #333;
  border-radius: 4px;
  padding: 6px 10px;
  color: #ddd;
  font-size: 13px;
  text-align: center;
}

.analyze-btn {
  padding: 6px 16px;
  background: #2979ff;
  color: #fff;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
}
.analyze-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.error-msg {
  color: #f44336;
  font-size: 12px;
  margin-top: 4px;
}

.result-block {
  margin-bottom: 20px;
  padding: 12px;
  background: #111;
  border-radius: 6px;
}

.charts-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
</style>
