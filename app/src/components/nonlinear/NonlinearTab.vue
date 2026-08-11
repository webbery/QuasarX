<template>
  <div class="nonlinear-tab">
    <!-- 顶部控制栏 -->
    <AnalysisControlBar
      v-model:mode="mode"
      v-model:selectedStrategyId="selectedStrategyId"
      v-model:selectedMacroCountry="selectedMacroCountry"
      v-model:selectedMacroIndicator="selectedMacroIndicator"
      v-model:quickRange="state.quickRange"
      :strategy-options="strategyOptions"
      :available-securities="availableSecurities"
      :checked-symbols="checkedSymbols"
      :filtered-macro-options="filteredMacroOptions"
      :quick-ranges="QUICK_RANGES"
      :loading="loading"
      :can-analyze="canAnalyze"
      @update:quickRange="setQuickRange($event)"
      @update-date-range="updateDateRange"
      @toggle-symbol="toggleSymbol"
      @run-analysis="runAnalysis"
    >
      <template #extra-controls>
        <div class="field-selector">
          <label>字段:</label>
          <select v-model="state.field" class="select-small">
            <option value="close">C 收盘价</option>
            <option value="open">O 开盘价</option>
            <option value="high">H 最高价</option>
            <option value="low">L 最低价</option>
            <option value="volume">V 成交量</option>
          </select>
        </div>

        <div class="param-group">
          <label>嵌入维度:</label>
          <input v-model.number="state.embedDim" type="number" min="2" max="7" class="number-input-small" />
        </div>

        <div class="param-group">
          <label>时间延迟:</label>
          <input v-model.number="state.timeDelay" type="number" min="0" max="100" class="number-input-small" title="0 = 自动(互信息法)" />
        </div>

        <div class="param-group">
          <label>Lyapunov:</label>
          <input v-model.number="state.lyapunovHorizon" type="number" min="10" max="200" class="number-input-small" />
        </div>
      </template>
    </AnalysisControlBar>

    <!-- 分析结果 -->
    <div v-if="result" class="results">
      <!-- 诊断摘要 -->
      <section class="diagnosis-section">
        <div class="diagnosis-cards">
          <div class="diag-card">
            <span class="diag-label">
              Hurst 指数
              <span class="tip-icon" title="Hurst 指数 (H) 衡量时间序列的长期记忆性。&#10;H > 0.5：持久性（趋势延续），涨后倾向再涨，适合趋势跟踪策略。&#10;H < 0.5：反持久性（均值回复），涨后倾向回落，适合反转/配对策略。&#10;H ≈ 0.5：随机游走，无预测价值，不宜做技术分析。">?</span>
            </span>
            <span class="diag-value" :class="{ persistent: result.mmar.hurst > 0.5, anti: result.mmar.hurst < 0.5 }">
              {{ result.mmar.hurst.toFixed(4) }}
            </span>
            <span class="diag-hint">{{ result.mmar.hurst > 0.5 ? '持久性' : result.mmar.hurst < 0.5 ? '反持久性' : '随机' }}</span>
          </div>
          <div class="diag-card">
            <span class="diag-label">
              多分形谱宽 Δα
              <span class="tip-icon" title="多分形谱宽度反映价格波动的不均匀程度。&#10;Δα 大（>0.1）：市场存在多重标度行为，极端涨跌事件频率偏离正态分布，尾部风险高。&#10;Δα 小（≤0.1）：波动较均匀，接近单分形（几何布朗运动），传统 Black-Scholes 模型适用。&#10;实战意义：谱宽大时应降低仓位、放宽止损，避免被极端行情击穿。">?</span>
            </span>
            <span class="diag-value">{{ result.mmar.width.toFixed(4) }}</span>
            <span class="diag-hint">{{ result.mmar.width > 0.1 ? '强多分形' : '弱多分形' }}</span>
          </div>
          <div class="diag-card">
            <span class="diag-label">
              关联维数 D₂
              <span class="tip-icon" title="关联维数衡量吸引子的几何复杂度，即驱动价格所需的最少自由度个数。&#10;D₂ 低（1~3）：价格由少数变量主导，系统接近低维确定性混沌，可用非线性模型（如 RBF、LSTM）预测。&#10;D₂ 高（>5）：系统接近随机，有效预测困难。&#10;D₂ 收敛（随嵌入维 m 增加不再增长）→ 存在确定性吸引子的证据。">?</span>
            </span>
            <span class="diag-value">{{ result.phase_space.correlation_dimension.toFixed(3) }}</span>
            <span class="diag-hint">m = {{ result.phase_space.embed_dim }}</span>
          </div>
          <div class="diag-card">
            <span class="diag-label">
              最大 Lyapunov λ
              <span class="tip-icon" title="最大 Lyapunov 指数量化混沌系统对初始条件的敏感程度。&#10;λ > 0：混沌特征，相邻轨迹指数分离，预测 horizon ≈ 1/λ 个采样周期。&#10;λ ≈ 0：周期性或准周期性行为，可预测。&#10;λ < 0：收敛到不动点，系统稳定。&#10;实战意义：λ 越大，短期预测窗口越短，高频信号衰减越快，应降低交易频率。">?</span>
            </span>
            <span class="diag-value" :class="{ positive: result.phase_space.max_lyapunov > 0.001 }">
              {{ result.phase_space.max_lyapunov.toFixed(4) }}
            </span>
            <span class="diag-hint">{{ result.phase_space.max_lyapunov > 0.001 ? '敏感依赖' : '非混沌' }}</span>
          </div>
          <div class="diag-card full-width">
            <span class="diag-label">
              诊断
              <span class="tip-icon" title="综合以上指标给出市场状态判定：&#10;• 确定性混沌：D₂ 收敛 + λ>0 → 短期可预测，适合非线性模型。&#10;• 随机游走：H≈0.5 + D₂ 不收敛 → 技术分析和量化择时均无效。&#10;• 趋势/均值回复：H 显著偏离 0.5 → 可选择对应策略类型。&#10;• 高风险：Δα 大 + λ 大 → 市场极端事件频繁，建议降低风险敞口。">?</span>
            </span>
            <span class="diag-diagnosis" :class="{ deterministic: result.phase_space.is_deterministic }">
              {{ result.phase_space.diagnosis }}
            </span>
          </div>
        </div>
      </section>

      <!-- MMAR 多分形分析 -->
      <section class="section">
        <h3 class="section-title">
          MMAR 多分形分析
          <span class="tip-icon" title="Multifractal Detrended Fluctuation Analysis (MF-DFA)。&#10;通过不同阶矩 q 的标度行为揭示价格波动的多重分形结构。&#10;&#10;左图 τ(q)：质量指数标度函数，反映不同波动幅度的标度规律。&#10;  非线性 → 多分形特征；线性 → 单分形。&#10;&#10;右图 h(q)：广义 Hurst 指数，不同波动幅度的持续性度量。&#10;  h(q) 随 q 递减 → 大波动和小波动具有不同的记忆性。&#10;  h(2) ≈ 经典 Hurst 指数。&#10;&#10;谱宽 Δα = h(-2) - h(2)：越大说明大小波动的标度行为差异越大，尾部风险越高。">?</span>
        </h3>
        <div class="chart-grid">
          <div class="chart-card">
            <div class="chart-label">τ(q) 质量指数标度函数<span class="tip-icon" title="τ(q) 描述不同阶矩 q 的标度指数。&#10;q > 0 侧重高波动区域，q < 0 侧重低波动区域。&#10;τ(q) 与 q 呈线性 → 单分形（简单标度）；&#10;τ(q) 非线性 → 多分形（复杂标度），需要完整的谱来描述。&#10;非线性程度越大，多分形特征越强。">?</span></div>
            <MMARChart :data="result.mmar" />
          </div>
          <div class="chart-card">
            <div class="chart-label">h(q) 广义 Hurst 指数<span class="tip-icon" title="h(q) 是不同波动幅度下的局部 Hurst 指数。&#10;h(q) > 0.5 → 该幅度波动具有持久性（趋势延续）；&#10;h(q) < 0.5 → 反持久性（均值回复）；&#10;h(q) 随 q 单调递减 → 大波动与小波动的持续性不同。&#10;Δh = h(-2) - h(2) 即为多分形谱宽的另一种表达。">?</span></div>
            <HqChart :data="result.mmar" />
          </div>
        </div>
      </section>

      <!-- 相空间重构 -->
      <section class="section">
        <h3 class="section-title">
          相空间重构与吸引子检测
          <span class="tip-icon" title="Takens 嵌入定理：用单变量时间序列的时延副本重构高维相空间，恢复动力系统几何结构。&#10;&#10;左图 相空间轨迹：嵌入维度 m 和时间延迟 τ 下的吸引子几何形态。&#10;  有结构 → 确定性系统；散乱 → 随机噪声。&#10;&#10;中图 关联维数 D₂：C(r) ~ r^D₂，衡量吸引子的分形维度。&#10;  D₂ 收敛（随 m 增加不再增长）→ 存在有限维确定性吸引子。&#10;&#10;右图 最大 Lyapunov 指数：相邻轨迹的平均发散速率。&#10;  λ > 0 → 混沌（对初始条件敏感），预测 horizon ≈ 1/λ。">?</span>
        </h3>
        <div class="chart-grid">
          <div class="chart-card">
            <div class="chart-label">相空间轨迹<span class="tip-icon" title="将一维时间序列 x(t) 用时间延迟 τ 展开为 m 维向量：&#10;X(t) = [x(t), x(t+τ), x(t+2τ), ..., x(t+(m-1)τ)]&#10;&#10;图中展示前两个主成分（PC1 vs PC2）的投影。&#10;轨迹形成有结构的几何图案 → 确定性吸引子；&#10;轨迹散乱无结构 → 随机过程。">?</span></div>
            <PhaseSpaceChart :data="result.phase_space" />
          </div>
          <div class="chart-card">
            <div class="chart-label">关联积分 C(r)<span class="tip-icon" title="关联积分 C(r) 衡量相空间中距离小于 r 的点对比例。&#10;在标度区 C(r) ~ r^D₂，斜率即为关联维数。&#10;&#10;log C(r) vs log r 图的线性段斜率 = D₂。&#10;D₂ 随嵌入维 m 增加而收敛 → 确定性系统的证据。&#10;D₂ 随 m 持续增大 → 随机过程（无限维）。">?</span></div>
            <CorrelationDimChart :data="result.phase_space" />
          </div>
        </div>
        <div class="chart-grid" style="margin-top: 12px;">
          <div class="chart-card">
            <div class="chart-label">Lyapunov 指数估计<span class="tip-icon" title="最大 Lyapunov 指数 λ₁ 量化相空间中相邻轨迹的平均指数发散率。&#10;&#10;图中展示 log(距离) 随时间的变化，线性段的斜率 = λ₁。&#10;λ₁ > 0 → 混沌系统，短期可预测但长期不可预测；&#10;λ₁ ≈ 0 → 周期性或准周期系统；&#10;λ₁ < 0 → 收敛到不动点，系统稳定。&#10;&#10;预测 horizon ≈ 1/λ₁ 个采样周期。">?</span></div>
            <LyapunovChart :data="result.phase_space" />
          </div>
          <div class="chart-card info-card">
            <h4>参数信息</h4>
            <div class="info-grid">
              <div class="info-item">
                <span class="info-label">数据点数</span>
                <span class="info-value">{{ result.data_points }}</span>
              </div>
              <div class="info-item">
                <span class="info-label">嵌入维度 m</span>
                <span class="info-value">{{ result.phase_space.embed_dim }}</span>
              </div>
              <div class="info-item">
                <span class="info-label">时间延迟 τ</span>
                <span class="info-value">{{ result.phase_space.time_delay }} ({{ result.phase_space.delay_method === 'mi' ? '互信息法' : '固定值' }})</span>
              </div>
              <div class="info-item">
                <span class="info-label">轨迹点数</span>
                <span class="info-value">{{ result.phase_space.trajectory.length }}</span>
              </div>
            </div>
          </div>
        </div>
      </section>
    </div>

    <!-- 空状态 -->
    <div v-else-if="!loading" class="empty-state">
      <p>选择标的和参数后点击"开始分析"</p>
      <p class="hint">支持 MMAR 多分形分析和相空间重构吸引子检测</p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useNonlinearState, QUICK_RANGES } from './composables/useNonlinearState'
import { useNonlinearData } from './composables/useNonlinearData'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import { useMacroIndicators } from '../shared/composables/useMacroIndicators'
import AnalysisControlBar from '../shared/AnalysisControlBar.vue'
import MMARChart from './charts/MMARChart.vue'
import HqChart from './charts/HqChart.vue'
import PhaseSpaceChart from './charts/PhaseSpaceChart.vue'
import CorrelationDimChart from './charts/CorrelationDimChart.vue'
import LyapunovChart from './charts/LyapunovChart.vue'

const { state, result, setQuickRange } = useNonlinearState()
const { loading: fetchLoading, fetchNonlinear } = useNonlinearData()
const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities()
const { macroOptionsByCountry } = useMacroIndicators()

type AnalysisMode = 'strategy' | 'macro'
const mode = ref<AnalysisMode>('strategy')
const selectedMacroCountry = ref('china')
const selectedMacroIndicator = ref('')

const filteredMacroOptions = computed(() =>
  macroOptionsByCountry.value[selectedMacroCountry.value] || []
)

const loading = computed(() => securitiesLoading.value || fetchLoading.value)

const canAnalyze = computed(() => {
  if (loading.value) return false
  if (mode.value === 'macro') return !!selectedMacroIndicator.value
  return checkedSymbols.value.size > 0
})

watch(checkedSymbols, (next) => {
  if (mode.value !== 'strategy') return
  // 非线性分析只取第一个标的
  if (next.size > 1) {
    const first = next.values().next().value as string
    checkedSymbols.value = new Set([first])
  }
})

watch(selectedStrategyId, (newId) => {
  if (newId) {
    loadSecuritiesForStrategy(newId)
  } else {
    availableSecurities.value = []
    checkedSymbols.value = new Set()
  }
})

watch([selectedMacroCountry, selectedMacroIndicator], ([country, indicator]) => {
  if (mode.value !== 'macro' || !indicator) return
  // macro mode
})

function updateDateRange(value: string, type: 'start' | 'end') {
  if (state.dateRange) {
    state.dateRange = type === 'start'
      ? [value, state.dateRange[1]]
      : [state.dateRange[0], value]
  }
}

async function runAnalysis() {
  let symbol = ''
  if (mode.value === 'macro') {
    symbol = `${selectedMacroCountry.value}/${selectedMacroIndicator.value}`
  } else {
    const first = Array.from(checkedSymbols.value)[0]
    if (!first) return
    symbol = first
  }

  if (!state.dateRange) return

  const res = await fetchNonlinear(
    symbol,
    state.dateRange[0],
    state.dateRange[1],
    state.field,
    {
      qMin: state.qMin,
      qMax: state.qMax,
      qStep: state.qStep,
      minWindow: state.minWindow,
      embedDim: state.embedDim,
      timeDelay: state.timeDelay,
      lyapunovHorizon: state.lyapunovHorizon,
    }
  )
  if (res) result.value = res
}
</script>

<style scoped>
.nonlinear-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow-y: auto;
  padding: 12px;
}

.nonlinear-tab::-webkit-scrollbar { width: 6px; }
.nonlinear-tab::-webkit-scrollbar-track { background: transparent; }
.nonlinear-tab::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}
.nonlinear-tab::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
}

.results {
  display: flex;
  flex-direction: column;
  gap: 16px;
  margin-top: 12px;
}

.section {
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  border: 1px solid var(--border, #333);
  padding: 16px;
}

.section-title {
  margin: 0 0 12px 0;
  font-size: 1rem;
  color: var(--text, #e0e0e0);
  font-weight: 600;
}

.chart-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.chart-card {
  background: rgba(0, 0, 0, 0.15);
  border-radius: 6px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  padding: 8px;
  min-height: 320px;
}

.chart-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 0.75rem;
  color: var(--text-secondary, #999);
  padding: 2px 4px 6px;
  letter-spacing: 0.3px;
}

/* 诊断摘要卡片 */
.diagnosis-section {
  margin-bottom: 4px;
}

.diagnosis-cards {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.diag-card {
  flex: 1;
  min-width: 140px;
  background: rgba(0, 0, 0, 0.3);
  border-radius: 8px;
  border: 1px solid var(--border, #333);
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.diag-card.full-width {
  flex: 1 1 100%;
}

.diag-label {
  font-size: 0.75rem;
  color: var(--text-secondary, #999);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  display: flex;
  align-items: center;
  gap: 4px;
}

.tip-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.08);
  color: var(--text-secondary, #888);
  font-size: 0.6rem;
  cursor: help;
  text-transform: none;
  letter-spacing: 0;
  flex-shrink: 0;
}

.tip-icon:hover {
  background: rgba(255, 255, 255, 0.15);
  color: var(--text, #ccc);
}

.diag-value {
  font-size: 1.3rem;
  font-weight: 700;
  color: var(--text, #e0e0e0);
  font-family: 'Courier New', monospace;
}

.diag-value.persistent { color: #00c853; }
.diag-value.anti { color: #ff6d00; }
.diag-value.positive { color: #ff1744; }

.diag-hint {
  font-size: 0.75rem;
  color: var(--text-secondary, #666);
}

.diag-diagnosis {
  font-size: 0.9rem;
  color: var(--text-secondary, #999);
  padding: 4px 0;
}

.diag-diagnosis.deterministic {
  color: #00c853;
  font-weight: 600;
}

/* 信息卡片 */
.info-card {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.info-card h4 {
  margin: 0;
  font-size: 0.95rem;
  color: var(--text, #e0e0e0);
}

.info-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.info-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.info-label {
  font-size: 0.75rem;
  color: var(--text-secondary, #999);
}

.info-value {
  font-size: 0.9rem;
  color: var(--text, #e0e0e0);
  font-family: 'Courier New', monospace;
}

/* 控制栏扩展 */
.field-selector,
.param-group {
  display: flex;
  align-items: center;
  gap: 4px;
}

.field-selector label,
.param-group label {
  font-size: 0.8rem;
  color: var(--text-secondary, #999);
  white-space: nowrap;
}

.select-small,
.number-input-small {
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, #444);
  border-radius: 4px;
  color: var(--text, #e0e0e0);
  padding: 4px 6px;
  font-size: 0.8rem;
  width: 60px;
}

.number-input-small {
  width: 55px;
}

/* 空状态 */
.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 300px;
  color: var(--text-secondary, #666);
}

.empty-state p {
  margin: 4px 0;
}

.empty-state .hint {
  font-size: 0.85rem;
  color: var(--text-secondary, #555);
}

@media (max-width: 900px) {
  .chart-grid {
    grid-template-columns: 1fr;
  }
}
</style>
