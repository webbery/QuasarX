<template>
  <div class="xgboost-tab">
    <!-- 顶部：策略选择 -->
    <header class="xgboost-header">
      <StrategySelector
        v-if="false"
        :selected-strategy-id="state.selectedStrategyId"
        :strategy-options="strategyOptions"
        :available-securities="availableSecurities"
        :checked-symbols="checkedSymbols"
        @update:selectedStrategyId="onStrategyChange"
        @toggle-symbol="toggleSymbol"
      />
      <div class="header-row">
        <label>选择策略:</label>
        <select v-model="state.selectedStrategyId" class="strategy-select" @change="onStrategyChange">
          <option value="">— 请选择策略 —</option>
          <option v-for="opt in strategyOptions" :key="opt.id" :value="opt.id">
            {{ opt.name }}
          </option>
        </select>
        <span v-if="!state.selectedStrategyId" class="hint">
          👈 请选择一个包含 XGBoost 节点的策略
        </span>
        <span v-else-if="!script" class="hint warn">
          ⚠ 当前策略不含 XGBoost 节点
        </span>
      </div>
    </header>

    <!-- 内容 -->
    <div v-if="!state.selectedStrategyId" class="empty-tab">
      <div class="empty-icon">🧠</div>
      <h2>XGBoost 训练与分析</h2>
      <p>选择一个使用 XGBoost 节点的策略后开始训练</p>
      <p class="note">支持的训练流程：<br />
        • 部分回测：仅执行 XGBoost 上游节点收集特征数据<br />
        • 离线训练：调用 xgboost (Python) 训练模型<br />
        • SHAP 分析：C++ 端 XGBoost C API 计算特征贡献
      </p>
    </div>

    <div v-else-if="!hasXGBoost" class="empty-tab">
      <div class="empty-icon">⚠️</div>
      <h2>策略中未找到 XGBoost 节点</h2>
      <p>请在策略图中添加 XGBoost 节点后再使用此面板</p>
    </div>

    <template v-else>
      <el-tabs v-model="activeTab" class="sub-tabs">
        <el-tab-pane label="训练" name="train">
          <TrainPanel :state="state" :script="script" @trained="onTrained" />
        </el-tab-pane>
        <el-tab-pane label="特征分析" name="feature">
          <FeaturePanel :state="state" />
        </el-tab-pane>
        <el-tab-pane label="诊断" name="diagnostic">
          <DiagnosticPanel :state="state" />
        </el-tab-pane>
      </el-tabs>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, onMounted, watch } from 'vue'
import { useXGBoostState } from './composables/useXGBoostState'
import { useXGBoostData } from './composables/useXGBoostData'
import TrainPanel from './panels/TrainPanel.vue'
import FeaturePanel from './panels/FeaturePanel.vue'
import DiagnosticPanel from './panels/DiagnosticPanel.vue'
import { useStrategySecurities } from '@/components/shared/composables/useStrategySecurities'
import { useHistoryStore } from '@/stores/history'

const activeTab = ref('train')
const state = useXGBoostState()

const { strategyOptions, availableSecurities, checkedSymbols, toggleSymbol } = useStrategySecurities()
const historyStore = useHistoryStore()

const script = ref('')
const hasXGBoost = ref(false)

const { listFeatureColumns } = useXGBoostData()

async function onStrategyChange() {
  state.reset()
  script.value = ''
  hasXGBoost.value = false
  if (!state.selectedStrategyId) return
  try {
    const versions = await historyStore.getVersionsByStrategy(state.selectedStrategyId.value)
    const latest = versions[0]
    if (!latest) return
    const data = await historyStore.loadVersionFlowData(latest.id)
    if (!data) return
    const graphData = typeof data === 'string' ? JSON.parse(data) : data
    script.value = JSON.stringify(graphData)
    hasXGBoost.value = graphData.nodes?.some((n: any) => n?.data?.nodeType === 'xgboost') ?? false
  } catch (e) {
    console.error('[XGBoostTab] 加载策略失败:', e)
  }
}

function onTrained() {
  activeTab.value = 'feature'
}

onMounted(() => {
  if (state.selectedStrategyId) onStrategyChange()
})
</script>

<style scoped>
.xgboost-tab { padding: 8px; }
.xgboost-header {
  padding: 12px 16px;
  background: #1e2a3a;
  border-radius: 8px;
  margin-bottom: 16px;
}
.header-row { display: flex; align-items: center; gap: 12px; }
.header-row label { color: #cbd5e1; font-size: 13px; }
.strategy-select {
  flex: 1;
  max-width: 400px;
  background: #0f1929;
  color: #e0e6f0;
  border: 1px solid #2b3a55;
  border-radius: 4px;
  padding: 6px 10px;
  font-size: 13px;
}
.hint { color: #64748b; font-size: 12px; }
.hint.warn { color: #f59e0b; }
.empty-tab {
  padding: 80px 20px;
  text-align: center;
  color: #64748b;
}
.empty-tab h2 { color: #cbd5e1; margin: 16px 0 8px; }
.empty-tab .note {
  margin-top: 24px;
  font-size: 13px;
  color: #94a3b8;
  line-height: 1.8;
}
.empty-icon { font-size: 64px; margin-bottom: 16px; }
.sub-tabs {
  background: #131c2e;
  border-radius: 8px;
  padding: 8px 16px;
}
:deep(.el-tabs__item) {
  color: #94a3b8;
  font-size: 14px;
}
:deep(.el-tabs__item.is-active) {
  color: #3b82f6;
}
</style>
