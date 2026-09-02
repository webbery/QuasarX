<template>
  <div class="derivative-container">
    <!-- 左侧面板 -->
    <div class="left-panel">
      <!-- 合约选择 -->
      <div class="section">
        <div class="section-title">合约选择</div>
        <div class="form-row">
          <label>交易所</label>
          <select v-model="filters.exchange" @change="onFilterChange">
            <option value="">全部</option>
            <option value="CFFEX">CFFEX</option>
            <option value="SSE">SSE</option>
            <option value="SZSE">SZSE</option>
          </select>
        </div>
        <div class="form-row">
          <label>标的</label>
          <select v-model="filters.product" @change="onFilterChange">
            <option value="">全部</option>
            <option v-for="p in availableProducts" :key="p" :value="p">{{ p }}</option>
          </select>
        </div>
        <div class="contract-list">
          <div v-for="c in filteredContracts" :key="c.symbol_id"
            class="contract-item" :class="{ selected: isSelected(c) }"
            @click="toggleContract(c)">
            <span class="contract-name">{{ c.contract_name }}</span>
            <span class="contract-strike">{{ c.strike_price }}</span>
            <span :class="['cp-badge', c.call_put === '认购' ? 'call' : 'put']">
              {{ c.call_put === '认购' ? 'C' : 'P' }}
            </span>
          </div>
          <div v-if="filteredContracts.length === 0" class="empty-hint">
            无合约数据，请先在数据中心下载
          </div>
        </div>
      </div>

      <!-- 参数配置 -->
      <div class="section">
        <div class="section-title">定价参数</div>
        <div class="form-row">
          <label>标的价格 S</label>
          <input type="number" v-model.number="params.spot" step="0.001" />
        </div>
        <div class="form-row">
          <label>行权价 K</label>
          <input type="number" v-model.number="params.strike" step="0.01" />
        </div>
        <div class="form-row">
          <label>到期日</label>
          <input type="date" v-model="params.expiry" />
        </div>
        <div class="form-row">
          <label>波动率 σ</label>
          <input type="number" v-model.number="params.volatility" step="0.001" min="0.001" />
        </div>
        <div class="form-row">
          <label>无风险利率</label>
          <input type="number" v-model.number="params.risk_free_rate" step="0.001" />
        </div>
        <div class="form-row">
          <label>定价方法</label>
          <select v-model="params.method">
            <option value="black_scholes">Black-Scholes</option>
            <option value="monte_carlo">Monte Carlo</option>
            <option value="binomial">二叉树 (CRR)</option>
          </select>
        </div>
        <template v-if="params.method === 'monte_carlo'">
          <div class="form-row">
            <label>模拟路径数</label>
            <input type="number" v-model.number="params.n_paths" step="10000" min="1000" />
          </div>
        </template>
        <template v-if="params.method === 'binomial'">
          <div class="form-row">
            <label>步数</label>
            <input type="number" v-model.number="params.n_steps" step="50" min="10" />
          </div>
          <div class="form-row">
            <label>美式行权</label>
            <input type="checkbox" v-model="params.is_american" />
          </div>
        </template>
        <button class="btn-calc" @click="calculate" :disabled="calculating">
          {{ calculating ? '计算中...' : '计算定价' }}
        </button>
      </div>

      <!-- 结果卡片 -->
      <div class="section" v-if="result">
        <div class="section-title">定价结果</div>
        <div class="result-grid">
          <div class="result-item highlight">
            <span class="result-label">理论价格</span>
            <span class="result-value">{{ result.price.toFixed(4) }}</span>
          </div>
          <div class="result-item">
            <span class="result-label">内在价值</span>
            <span class="result-value">{{ result.intrinsic_value.toFixed(4) }}</span>
          </div>
          <div class="result-item">
            <span class="result-label">时间价值</span>
            <span class="result-value">{{ result.time_value.toFixed(4) }}</span>
          </div>
          <div class="result-item">
            <span class="result-label">虚实度</span>
            <span :class="['moneyness-badge', result.moneyness.toLowerCase()]">
              {{ moneynessLabel(result.moneyness) }}
            </span>
          </div>
        </div>
        <div class="greeks-grid">
          <div class="greek-item" v-for="(val, key) in result.greeks" :key="key">
            <span class="greek-name">{{ key }}</span>
            <span class="greek-val">{{ formatGreek(key as string, val) }}</span>
          </div>
        </div>
        <div v-if="result.mc_std_error !== undefined" class="extra-info">
          MC 标准误差: {{ result.mc_std_error.toFixed(6) }}
        </div>
        <div v-if="result.early_exercise_premium !== undefined && result.early_exercise_premium > 0" class="extra-info">
          提前行权溢价: {{ result.early_exercise_premium.toFixed(4) }}
        </div>
      </div>
    </div>

    <!-- 右侧图表区 -->
    <div class="right-charts">
      <div class="chart-tabs">
        <button v-for="tab in chartTabs" :key="tab.key"
          :class="['chart-tab', { active: activeChart === tab.key }]"
          @click="activeChart = tab.key">
          {{ tab.label }}
        </button>
      </div>
      <div class="chart-content">
        <PayoffChart v-show="activeChart === 'payoff'"
          :result="result" :multi-results="multiResults" :params="params" />
        <IVSurfaceChart v-show="activeChart === 'iv'"
          :exchange="filters.exchange || 'SSE'" :product="filters.product || '50ETF'" />
        <GreeksChart v-show="activeChart === 'greeks'"
          :result="result" :params="params" />
        <MultiContractCompare v-show="activeChart === 'compare'"
          :contracts="selectedContracts" :params="params"
          @update:results="(r) => multiResults = r" />
        <StrategyBuilder v-show="activeChart === 'strategy'"
          :spot="params.spot" :risk-free-rate="params.risk_free_rate" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import {
  priceOption, listOptionContracts,
  type PricingRequest, type PricingResult, type ContractInfo
} from './composables/useOptionPricing'
import PayoffChart from './panels/PayoffChart.vue'
import IVSurfaceChart from './panels/IVSurfaceChart.vue'
import GreeksChart from './panels/GreeksChart.vue'
import MultiContractCompare from './panels/MultiContractCompare.vue'
import StrategyBuilder from './panels/StrategyBuilder.vue'

const chartTabs = [
  { key: 'payoff', label: '收益图' },
  { key: 'iv', label: 'IV 曲面' },
  { key: 'greeks', label: 'Greeks' },
  { key: 'compare', label: '多合约对比' },
  { key: 'strategy', label: '策略组合' },
]

const filters = ref({ exchange: '', product: '' })
const contracts = ref<ContractInfo[]>([])
const selectedContracts = ref<ContractInfo[]>([])
const result = ref<PricingResult | null>(null)
const multiResults = ref<PricingResult[]>([])
const calculating = ref(false)
const activeChart = ref('payoff')

const params = ref({
  spot: 3.2,
  strike: 3.2,
  expiry: '',
  volatility: 0.2,
  risk_free_rate: 0.015,
  dividend_yield: 0,
  method: 'black_scholes' as PricingRequest['method'],
  is_call: true,
  is_american: false,
  n_paths: 100000,
  n_steps: 200,
})

const availableProducts = computed(() => {
  const set = new Set(contracts.value.map(c => c.product))
  return [...set].sort()
})

const filteredContracts = computed(() => {
  return contracts.value.filter(c => {
    if (filters.value.exchange && c.exchange !== filters.value.exchange) return false
    if (filters.value.product && c.product !== filters.value.product) return false
    return true
  })
})

function isSelected(c: ContractInfo) {
  return selectedContracts.value.some(s => s.symbol_id === c.symbol_id)
}

function toggleContract(c: ContractInfo) {
  const idx = selectedContracts.value.findIndex(s => s.symbol_id === c.symbol_id)
  if (idx >= 0) {
    selectedContracts.value.splice(idx, 1)
  } else {
    selectedContracts.value.push(c)
  }
  // 用第一个选中合约填充参数
  if (selectedContracts.value.length > 0) {
    const first = selectedContracts.value[0]
    params.value.strike = first.strike_price
    params.value.is_call = first.call_put === '认购'
    // 从合约名解析到期日
    const m = first.contract_name.match(/(\d{2})(\d{2})/)
    if (m) {
      const year = 2000 + parseInt(m[1])
      const month = parseInt(m[2])
      params.value.expiry = `${year}-${String(month).padStart(2, '0')}-17`
    }
  }
}

async function onFilterChange() {
  contracts.value = await listOptionContracts(filters.value.exchange || undefined, filters.value.product || undefined)
}

async function calculate() {
  if (!params.value.strike || !params.value.spot) return
  calculating.value = true
  try {
    const req: PricingRequest = {
      method: params.value.method,
      spot: params.value.spot,
      strike: params.value.strike,
      volatility: params.value.volatility,
      risk_free_rate: params.value.risk_free_rate,
      dividend_yield: params.value.dividend_yield,
      is_call: params.value.is_call,
      is_american: params.value.is_american,
      n_paths: params.value.n_paths,
      n_steps: params.value.n_steps,
    }
    if (params.value.expiry) {
      req.expiry = params.value.expiry
    }
    result.value = await priceOption(req)
  } catch (e: any) {
    console.error('Pricing failed:', e)
  } finally {
    calculating.value = false
  }
}

function moneynessLabel(m: string) {
  return m === 'ITM' ? '实值' : m === 'OTM' ? '虚值' : '平值'
}

function formatGreek(key: string, val: number) {
  if (key === 'theta' || key === 'rho') return val.toFixed(4)
  return val.toFixed(4)
}

onMounted(async () => {
  await onFilterChange()
})
</script>

<style scoped>
.derivative-container {
  height: 90vh;
  display: flex;
  background: #1a2236;
  color: #e0e0e0;
  font-family: 'Helvetica Neue', Arial, sans-serif;
}

.left-panel {
  width: 320px;
  min-width: 320px;
  border-right: 1px solid rgba(74, 85, 104, 0.3);
  overflow-y: auto;
  padding: 12px;
}

.section {
  margin-bottom: 16px;
}

.section-title {
  font-size: 13px;
  font-weight: 600;
  color: #8899bb;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 8px;
  padding-bottom: 4px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
}

.form-row {
  display: flex;
  align-items: center;
  margin-bottom: 6px;
  gap: 8px;
}

.form-row label {
  font-size: 12px;
  color: #8899bb;
  min-width: 72px;
  flex-shrink: 0;
}

.form-row select, .form-row input[type="number"], .form-row input[type="date"] {
  flex: 1;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  padding: 4px 8px;
  font-size: 12px;
  outline: none;
}

.form-row select:focus, .form-row input:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.form-row select option {
  background: #1a2236;
}

.contract-list {
  max-height: 200px;
  overflow-y: auto;
  margin-top: 8px;
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 4px;
}

.contract-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 8px;
  cursor: pointer;
  font-size: 12px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.1);
}

.contract-item:hover {
  background: rgba(41, 98, 255, 0.1);
}

.contract-item.selected {
  background: rgba(41, 98, 255, 0.2);
  border-left: 2px solid #2962ff;
}

.contract-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.contract-strike {
  color: #8899bb;
  font-size: 11px;
}

.cp-badge {
  font-size: 10px;
  padding: 1px 4px;
  border-radius: 2px;
  font-weight: 600;
}

.cp-badge.call { background: rgba(76, 175, 80, 0.2); color: #66bb6a; }
.cp-badge.put { background: rgba(239, 83, 80, 0.2); color: #ef5350; }

.empty-hint {
  padding: 12px;
  text-align: center;
  color: #556;
  font-size: 12px;
}

.btn-calc {
  width: 100%;
  margin-top: 8px;
  padding: 8px;
  background: #2962ff;
  color: white;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
  font-weight: 500;
}

.btn-calc:hover { background: #1e50d9; }
.btn-calc:disabled { opacity: 0.5; cursor: not-allowed; }

.result-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin-bottom: 10px;
}

.result-item {
  background: rgba(26, 34, 54, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 4px;
  padding: 8px;
  text-align: center;
}

.result-item.highlight {
  border-color: rgba(41, 98, 255, 0.4);
}

.result-label {
  display: block;
  font-size: 11px;
  color: #8899bb;
  margin-bottom: 4px;
}

.result-value {
  font-size: 16px;
  font-weight: 600;
  color: #e0e0e0;
}

.moneyness-badge {
  font-size: 13px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 4px;
}

.moneyness-badge.itm { background: rgba(76, 175, 80, 0.2); color: #66bb6a; }
.moneyness-badge.otm { background: rgba(239, 83, 80, 0.2); color: #ef5350; }
.moneyness-badge.atm { background: rgba(255, 193, 7, 0.2); color: #ffc107; }

.greeks-grid {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  gap: 4px;
}

.greek-item {
  text-align: center;
  padding: 4px;
  background: rgba(26, 34, 54, 0.4);
  border-radius: 3px;
}

.greek-name {
  display: block;
  font-size: 10px;
  color: #8899bb;
  text-transform: capitalize;
}

.greek-val {
  font-size: 12px;
  font-weight: 500;
}

.extra-info {
  margin-top: 6px;
  font-size: 11px;
  color: #8899bb;
}

/* 右侧图表 */
.right-charts {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.chart-tabs {
  display: flex;
  gap: 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  padding: 0 12px;
}

.chart-tab {
  padding: 10px 16px;
  background: none;
  border: none;
  color: #8899bb;
  font-size: 13px;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  transition: all 0.2s;
}

.chart-tab:hover { color: #e0e0e0; }
.chart-tab.active {
  color: #2962ff;
  border-bottom-color: #2962ff;
}

.chart-content {
  flex: 1;
  overflow: auto;
  padding: 12px;
}
</style>
