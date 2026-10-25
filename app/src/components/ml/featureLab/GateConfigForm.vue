<template>
  <div class="gate-config">
    <div class="gc-grid">
      <label class="gc-item">
        <span class="gc-label">
          预测周期 N
          <i class="tip" title="标签的前瞻收益周期（bar 数）。与标签分析、训练分析共用同一份配置。">?</i>
        </span>
        <input type="number" v-model.number="config.labelPeriod" min="1" max="60" class="gc-input" />
      </label>

      <label class="gc-item">
        <span class="gc-label">
          波动阈值 vol_k
          <i class="tip" title="threshold = vol_k × σ × √N。只影响三分类，不改变 rank IC。">?</i>
        </span>
        <input type="number" v-model.number="config.volK" min="0.1" max="2" step="0.1" class="gc-input" />
      </label>

      <label class="gc-item">
        <span class="gc-label">
          交叉验证折数
          <i class="tip" title="DEV 区间内切 N 个连续折，报每折 IC 与同号折数。">?</i>
        </span>
        <input type="number" v-model.number="config.nFolds" min="2" max="50" class="gc-input"
               @change="onNFoldsChange" />
      </label>

      <label class="gc-item">
        <span class="gc-label">
          折间间隔 gap
          <i class="tip" title="相邻折之间跳过的 bar 数，生效值 = max(gap, 标签周期)。">?</i>
        </span>
        <input type="number" v-model.number="config.foldGap" min="0" max="250" class="gc-input" />
        <span class="gc-hint" :class="{ warn: gapRaised }">生效 {{ effectiveGap }} bar</span>
      </label>
    </div>

    <label class="gc-item gc-sigma">
      <span class="gc-label">标签波动率口径</span>
      <select v-model="config.sigmaMode" class="gc-input">
        <option value="expanding">expanding（因果，无泄露）</option>
        <option value="full">full（全序列，旧行为）</option>
      </select>
    </label>

    <button class="gc-toggle" @click="showAdvanced = !showAdvanced">
      <i :class="showAdvanced ? 'fas fa-chevron-down' : 'fas fa-chevron-right'"></i>
      高级规则
      <span class="gc-hint">{{ rules.minIc }} / t {{ rules.minT }} / q {{ rules.fdrQ }}</span>
    </button>

    <div v-show="showAdvanced" class="gc-advanced">
      <div class="gc-grid">
        <label class="gc-item">
          <span class="gc-label">|IC| 门槛</span>
          <input type="number" v-model.number="rules.minIc" step="0.005" min="0" max="1" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">|NW t| 门槛</span>
          <input type="number" v-model.number="rules.minT" step="0.1" min="0" max="50" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">同号折数</span>
          <input type="number" v-model.number="rules.minSignFolds" min="0" :max="config.nFolds" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">FDR q 门槛</span>
          <input type="number" v-model.number="rules.fdrQ" step="0.01" min="0.01" max="1" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">覆盖率门槛</span>
          <input type="number" v-model.number="rules.minCoverage" step="0.05" min="0" max="1" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">top-k 门槛</span>
          <input type="number" v-model.number="rules.gateTopk" min="1" max="50" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">单日最少标的</span>
          <input type="number" v-model.number="rules.minNames" min="2" max="500" class="gc-input" />
        </label>
        <label class="gc-item">
          <span class="gc-label">
            历史试验次数
            <i class="tip" title="用于 FDR 校正；留 0 表示按本次特征数计。">?</i>
          </span>
          <input type="number" v-model.number="rules.trialCount" min="0" max="100000" class="gc-input" />
        </label>
      </div>

      <label class="gc-check">
        <input type="checkbox" v-model="rules.noTest" />
        <span>不计算锁定 TEST（只看 DEV）</span>
      </label>
      <label class="gc-check">
        <input type="checkbox" v-model="rules.noIncremental" />
        <span>跳过增量 IC</span>
      </label>
      <label class="gc-check">
        <input type="checkbox" v-model="rules.noLagSensitivity" />
        <span>跳过滞后敏感性</span>
      </label>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'

const props = defineProps<{ state: any }>()

const config = props.state.config
const rules = props.state.screenConfig
const showAdvanced = ref(false)

const effectiveGap = computed(() => Math.max(config.foldGap || 0, config.labelPeriod || 0))
const gapRaised = computed(() => (config.foldGap || 0) < (config.labelPeriod || 0))

function onNFoldsChange() {
  config.nFolds = Math.min(Math.max(Number(config.nFolds) || 2, 2), 50)
  if (rules.minSignFolds > config.nFolds) rules.minSignFolds = config.nFolds
}
</script>

<style scoped>
.gate-config { padding: 10px 12px 4px; }

.gc-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px 10px;
}

.gc-item { display: flex; flex-direction: column; gap: 3px; min-width: 0; }

.gc-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 10px;
  letter-spacing: 0.3px;
  color: #7d8c9e;
  white-space: nowrap;
}

.tip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 11px;
  height: 11px;
  border-radius: 50%;
  background: rgba(91, 143, 249, 0.18);
  color: #93c5fd;
  font-size: 8px;
  font-style: normal;
  cursor: help;
}

.gc-input {
  width: 100%;
  padding: 4px 6px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 3px;
  color: #dbe4ee;
  font-family: 'SF Mono', 'Cascadia Mono', 'Consolas', monospace;
  font-size: 11px;
  outline: none;
  box-sizing: border-box;
}
.gc-input:focus { border-color: #38bdf8; }

.gc-hint { font-size: 9.5px; color: #4f5d6d; }
.gc-hint.warn { color: #fbbf24; }

.gc-sigma { margin-top: 8px; }

.gc-toggle {
  margin-top: 10px;
  width: 100%;
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 5px 8px;
  background: rgba(91, 143, 249, 0.07);
  border: 1px solid rgba(74, 85, 104, 0.28);
  border-radius: 4px;
  color: #cbd5e1;
  font-family: inherit;
  font-size: 11px;
  cursor: pointer;
}
.gc-toggle .gc-hint { margin-left: auto; font-family: 'SF Mono', 'Consolas', monospace; }

.gc-advanced { margin-top: 9px; }

.gc-check {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 6px;
  font-size: 10.5px;
  color: #94a3b8;
  cursor: pointer;
}
.gc-check input { accent-color: #3b82f6; }
</style>
