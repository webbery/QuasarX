<template>
  <div class="imf-energy-card">
    <h3 class="card-title">IMF 能量分布</h3>
    <div class="energy-list">
      <div
        v-for="info in data?.imf_info"
        :key="info.index"
        class="energy-item"
      >
        <div class="energy-bar-wrapper">
          <span class="imf-label">IMF{{ info.index }}</span>
          <div class="energy-bar-bg">
            <div
              class="energy-bar-fill"
              :style="{ width: (info.energy_pct || 0) + '%', backgroundColor: IMF_COLORS[(info.index - 1) % IMF_COLORS.length] }"
            ></div>
          </div>
          <span class="energy-value">{{ (info.energy_pct || 0).toFixed(1) }}%</span>
        </div>
        <span class="period-label">T≈{{ info.mean_period?.toFixed(0) || '-' }}</span>
      </div>
    </div>
    <div class="reconstruction-error">
      重建误差: {{ (data?.reconstruction_error || 0).toExponential(2) }}
    </div>
  </div>
</template>

<script setup lang="ts">
import type { SignalAnalysisResult } from '../composables/useSignalState'

defineProps<{
  data: SignalAnalysisResult | null
}>()

const IMF_COLORS = [
  '#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0',
  '#e040fb', '#ffab40', '#69f0ae', '#ff5252', '#7c4dff',
  '#40c4ff', '#ffd740', '#b2ff59', '#ff6e40', '#ea80fc',
  '#18ffff', '#eeff41', '#ff80ab', '#b388ff', '#84ffff'
]
</script>

<style scoped>
.imf-energy-card {
  background: rgba(36, 46, 66, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 8px;
  padding: 16px;
}

.card-title {
  margin: 0 0 12px 0;
  font-size: 14px;
  color: #e0e0e0;
  font-weight: 600;
}

.energy-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.energy-item {
  display: flex;
  align-items: center;
  gap: 8px;
}

.energy-bar-wrapper {
  flex: 1;
  display: flex;
  align-items: center;
  gap: 8px;
}

.imf-label {
  font-size: 12px;
  color: #e0e0e0;
  min-width: 40px;
}

.energy-bar-bg {
  flex: 1;
  height: 16px;
  background: rgba(74, 85, 104, 0.2);
  border-radius: 3px;
  overflow: hidden;
}

.energy-bar-fill {
  height: 100%;
  border-radius: 3px;
  transition: width 0.3s ease;
}

.energy-value {
  font-size: 12px;
  color: #e0e0e0;
  min-width: 45px;
  text-align: right;
  font-weight: 600;
}

.period-label {
  font-size: 11px;
  color: #999;
  min-width: 35px;
}

.reconstruction-error {
  margin-top: 12px;
  font-size: 12px;
  color: #a0aec0;
  text-align: right;
}
</style>
