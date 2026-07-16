<template>
  <div class="capacity-tab">
    <!-- 控制栏 -->
    <div class="control-bar">
      <div class="control-group">
        <label>策略:</label>
        <select v-model="selectedStrategy" class="ctrl-select">
          <option value="">请选择策略</option>
          <option v-for="s in strategies" :key="s" :value="s">{{ s }}</option>
        </select>
      </div>
      <div class="control-group">
        <label>最小资金:</label>
        <input v-model.number="minCapital" type="number" class="ctrl-input small" min="10000" step="100000" />
      </div>
      <div class="control-group">
        <label>最大资金:</label>
        <input v-model.number="maxCapital" type="number" class="ctrl-input small" min="100000" step="1000000" />
      </div>
      <div class="control-group">
        <label>步数:</label>
        <input v-model.number="steps" type="number" class="ctrl-input xs" min="5" max="50" />
      </div>
      <div class="control-group">
        <label>η:</label>
        <input v-model.number="eta" type="number" class="ctrl-input xs" min="0.1" max="3" step="0.1" />
      </div>
      <div class="control-group">
        <label>ADV窗口:</label>
        <input v-model.number="advWindow" type="number" class="ctrl-input xs" min="5" max="60" />
      </div>
      <div class="control-group">
        <label>参与率上限:</label>
        <input v-model.number="maxParticipation" type="number" class="ctrl-input xs" min="0.01" max="0.2" step="0.01" />
      </div>
      <button class="run-btn" :disabled="!selectedStrategy || loading" @click="runScan">
        {{ loading ? '扫描中...' : '执行扫描' }}
      </button>
    </div>

    <!-- 错误提示 -->
    <div v-if="error" class="error-msg">{{ error }}</div>

    <!-- 结果区域 -->
    <div v-if="result" class="results">
      <CapacitySummaryCard :baseline="result.baseline" :summary="result.summary" />

      <div class="chart-section">
        <h4>Sharpe 衰减曲线</h4>
        <CapacityDecayChart :curve="result.capacity_curve" :baseline-sharpe="result.baseline.sharpe" />
      </div>

      <div class="chart-section">
        <h4>参与率 & 冲击成本</h4>
        <ParticipationChart :curve="result.capacity_curve" :max-participation="maxParticipation" />
      </div>

      <div class="chart-section">
        <h4>容量扫描明细</h4>
        <div class="table-wrapper">
          <table class="detail-table">
            <thead>
              <tr>
                <th>资金</th>
                <th>Sharpe</th>
                <th>总收益</th>
                <th>最大回撤</th>
                <th>胜率</th>
                <th>平均参与率</th>
                <th>最大参与率</th>
                <th>冲击(bps)</th>
                <th>衰减</th>
                <th>状态</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(p, i) in result.capacity_curve" :key="i" :class="rowClass(p)">
                <td>{{ formatCap(p.capital) }}</td>
                <td>{{ p.sharpe.toFixed(2) }}</td>
                <td :class="{ neg: p.total_return < 0 }">{{ (p.total_return * 100).toFixed(2) }}%</td>
                <td class="neg">{{ (p.max_drawdown * 100).toFixed(2) }}%</td>
                <td>{{ (p.win_rate * 100).toFixed(1) }}%</td>
                <td>{{ (p.avg_participation * 100).toFixed(3) }}%</td>
                <td>{{ (p.max_participation * 100).toFixed(3) }}%</td>
                <td>{{ p.avg_slippage_bps.toFixed(1) }}</td>
                <td>{{ (p.sharpe_decay * 100).toFixed(1) }}%</td>
                <td class="status">{{ statusIcon(p) }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </div>

    <!-- 空状态 -->
    <div v-else-if="!loading && !error" class="empty-state">
      <p>选择策略并点击"执行扫描"开始容量分析</p>
      <p class="hint">容量扫描会模拟不同资金规模下的市场冲击，评估策略可承载的最大资金量</p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import CapacitySummaryCard from './CapacitySummaryCard.vue'
import CapacityDecayChart from './CapacityDecayChart.vue'
import ParticipationChart from './ParticipationChart.vue'

interface CapacityPoint {
  capital: number
  sharpe: number
  total_return: number
  max_drawdown: number
  win_rate: number
  avg_participation: number
  max_participation: number
  avg_slippage_bps: number
  orders_above_limit: number
  sharpe_decay: number
}

interface CapacityResult {
  strategy: string
  baseline: { sharpe: number; total_return: number; max_drawdown: number; win_rate: number; n_trades: number }
  capacity_curve: CapacityPoint[]
  summary: { capacity_20pct: number; capacity_50pct: number; bottleneck_symbol: string; bottleneck_adv: number }
}

const strategies = ref<string[]>([])
const selectedStrategy = ref('')
const minCapital = ref(100000)
const maxCapital = ref(10000000)
const steps = ref(20)
const eta = ref(1.0)
const advWindow = ref(20)
const maxParticipation = ref(0.05)
const loading = ref(false)
const error = ref('')
const result = ref<CapacityResult | null>(null)

onMounted(async () => {
  try {
    const res = await axios.get('/v0/strategy')
    if (Array.isArray(res.data)) {
      strategies.value = res.data.map((s: any) => s.name || s)
    }
  } catch {
    // 策略列表加载失败不阻塞
  }
})

async function runScan() {
  if (!selectedStrategy.value) return
  loading.value = true
  error.value = ''
  result.value = null

  try {
    const res = await axios.post('/v0/capacity', {
      strategy: selectedStrategy.value,
      capital_range: {
        min: minCapital.value,
        max: maxCapital.value,
        steps: steps.value
      },
      impact_model: {
        eta: eta.value,
        adv_window: advWindow.value
      },
      constraints: {
        max_participation_rate: maxParticipation.value
      }
    })
    result.value = res.data
  } catch (e: any) {
    error.value = e.response?.data?.message || e.message || '扫描失败'
  } finally {
    loading.value = false
  }
}

function formatCap(v: number): string {
  if (v >= 1e8) return (v / 1e8).toFixed(2) + '亿'
  if (v >= 1e4) return (v / 1e4).toFixed(0) + '万'
  return v.toFixed(0)
}

function statusIcon(p: CapacityPoint): string {
  if (p.sharpe_decay >= 0.5) return '🔴'
  if (p.sharpe_decay >= 0.2) return '⚠️'
  return '✅'
}

function rowClass(p: CapacityPoint): string {
  if (p.sharpe_decay >= 0.5) return 'row-danger'
  if (p.sharpe_decay >= 0.2) return 'row-warning'
  return ''
}
</script>

<style scoped>
.capacity-tab {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 12px;
  height: 100%;
  overflow-y: auto;
}

.control-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: flex-end;
  padding: 10px 12px;
  background: var(--bg-secondary, #1e1e2e);
  border: 1px solid var(--border-color, #333);
  border-radius: 8px;
}
.control-group {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.control-group label {
  font-size: 11px;
  color: var(--text-secondary, #888);
}
.ctrl-select, .ctrl-input {
  background: var(--bg-tertiary, #2a2a3e);
  border: 1px solid var(--border-color, #444);
  border-radius: 4px;
  color: var(--text-primary, #e0e0e0);
  padding: 4px 8px;
  font-size: 12px;
}
.ctrl-select { min-width: 140px; }
.ctrl-input.small { width: 110px; }
.ctrl-input.xs { width: 70px; }

.run-btn {
  background: #4fc3f7;
  color: #000;
  border: none;
  border-radius: 4px;
  padding: 6px 16px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
}
.run-btn:disabled {
  background: #555;
  color: #999;
  cursor: not-allowed;
}
.run-btn:hover:not(:disabled) {
  background: #29b6f6;
}

.error-msg {
  background: rgba(239,83,80,0.15);
  border: 1px solid #ef5350;
  border-radius: 6px;
  padding: 10px;
  color: #ef5350;
  font-size: 13px;
}

.results {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.chart-section {
  background: var(--bg-secondary, #1e1e2e);
  border: 1px solid var(--border-color, #333);
  border-radius: 8px;
  padding: 12px;
}
.chart-section h4 {
  margin: 0 0 8px 0;
  font-size: 13px;
  color: var(--text-secondary, #aaa);
}

.table-wrapper {
  overflow-x: auto;
}
.detail-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}
.detail-table th {
  text-align: left;
  padding: 6px 8px;
  border-bottom: 1px solid var(--border-color, #444);
  color: var(--text-secondary, #888);
  font-weight: 500;
  white-space: nowrap;
}
.detail-table td {
  padding: 5px 8px;
  border-bottom: 1px solid var(--border-color, #2a2a3e);
  color: var(--text-primary, #ccc);
  white-space: nowrap;
}
.detail-table .neg { color: #ef5350; }
.detail-table .status { font-size: 14px; }
.row-warning { background: rgba(255,183,77,0.08); }
.row-danger { background: rgba(239,83,80,0.08); }

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 300px;
  color: var(--text-secondary, #666);
}
.empty-state .hint {
  font-size: 12px;
  color: #555;
  margin-top: 8px;
}
</style>
