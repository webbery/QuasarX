<template>
  <div class="multi-compare">
    <div class="toolbar">
      <button class="btn-run" @click="runCompare" :disabled="contracts.length === 0">
        批量定价 ({{ contracts.length }} 个合约)
      </button>
    </div>
    <div v-if="results.length === 0" class="empty-hint">
      选择多个合约后点击"批量定价"进行对比
    </div>
    <template v-else>
      <div class="compare-table">
        <table>
          <thead>
            <tr>
              <th>合约</th>
              <th>类型</th>
              <th>行权价</th>
              <th>价格</th>
              <th>Delta</th>
              <th>Gamma</th>
              <th>Theta</th>
              <th>Vega</th>
              <th>虚实度</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(r, i) in results" :key="i">
              <td>{{ contracts[i]?.contract_name || '-' }}</td>
              <td>
                <span :class="['cp-badge', r.is_call ? 'call' : 'put']">
                  {{ r.is_call ? 'C' : 'P' }}
                </span>
              </td>
              <td>{{ r.strike }}</td>
              <td class="num">{{ r.price.toFixed(4) }}</td>
              <td class="num">{{ r.greeks.delta.toFixed(4) }}</td>
              <td class="num">{{ r.greeks.gamma.toFixed(4) }}</td>
              <td class="num">{{ r.greeks.theta.toFixed(4) }}</td>
              <td class="num">{{ r.greeks.vega.toFixed(4) }}</td>
              <td>
                <span :class="['moneyness', r.moneyness.toLowerCase()]">{{ r.moneyness }}</span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div ref="chartRef" class="chart-area"></div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import { priceMultiOption, type PricingResult, type ContractInfo } from '../composables/useOptionPricing'

const props = defineProps<{
  contracts: ContractInfo[]
  params: { spot: number; volatility: number; risk_free_rate: number; method: string; expiry: string }
}>()

const emit = defineEmits<{ 'update:results': [results: PricingResult[]] }>()

const results = ref<PricingResult[]>([])
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

async function runCompare() {
  if (props.contracts.length === 0) return
  const contractParams = props.contracts.map(c => ({
    strike: c.strike_price,
    is_call: c.call_put === '认购',
    expiry: props.params.expiry || undefined,
  }))
  try {
    results.value = await priceMultiOption(
      props.params.spot, contractParams,
      props.params.method, props.params.volatility, props.params.risk_free_rate
    )
    emit('update:results', results.value)
    renderChart()
  } catch (e) {
    console.error('Multi pricing failed:', e)
  }
}

function renderChart() {
  if (!chart || results.value.length === 0) return

  const labels = results.value.map(r => `${r.strike} ${r.is_call ? 'C' : 'P'}`)

  chart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
    },
    legend: {
      top: 8,
      textStyle: { color: '#8899bb', fontSize: 11 },
    },
    grid: { left: 60, right: 30, top: 50, bottom: 50 },
    xAxis: {
      type: 'category', data: labels,
      axisLabel: { color: '#8899bb', fontSize: 10, rotate: 30 },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
    },
    yAxis: [
      {
        type: 'value', name: '价格',
        axisLabel: { color: '#8899bb' },
        splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
      },
      {
        type: 'value', name: 'Delta',
        axisLabel: { color: '#8899bb' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: '价格', type: 'bar',
        data: results.value.map(r => r.price),
        itemStyle: { color: 'rgba(41, 98, 255, 0.6)' },
      },
      {
        name: 'Delta', type: 'line', yAxisIndex: 1,
        data: results.value.map(r => r.greeks.delta),
        lineStyle: { width: 2 }, itemStyle: { color: '#ffa726' },
        symbol: 'circle', symbolSize: 6,
      },
    ],
  }, true)
}

function handleResize() { chart?.resize() }

onMounted(() => {
  if (chartRef.value) chart = echarts.init(chartRef.value)
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})
</script>

<style scoped>
.multi-compare { height: 100%; display: flex; flex-direction: column; }
.toolbar { padding: 8px 0; flex-shrink: 0; }
.btn-run {
  padding: 6px 16px; background: #2962ff; color: white;
  border: none; border-radius: 4px; cursor: pointer; font-size: 12px;
}
.btn-run:disabled { opacity: 0.5; cursor: not-allowed; }
.empty-hint { flex: 1; display: flex; align-items: center; justify-content: center; color: #556; font-size: 13px; }
.compare-table {
  overflow-x: auto; flex-shrink: 0; max-height: 200px; overflow-y: auto;
  margin-bottom: 12px;
}
.compare-table table {
  width: 100%; border-collapse: collapse; font-size: 12px;
}
.compare-table th {
  background: rgba(26, 34, 54, 0.8); color: #8899bb;
  padding: 6px 8px; text-align: left; position: sticky; top: 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
}
.compare-table td {
  padding: 4px 8px; border-bottom: 1px solid rgba(74, 85, 104, 0.1);
}
.compare-table .num { text-align: right; font-family: monospace; }
.cp-badge { font-size: 10px; padding: 1px 4px; border-radius: 2px; font-weight: 600; }
.cp-badge.call { background: rgba(76, 175, 80, 0.2); color: #66bb6a; }
.cp-badge.put { background: rgba(239, 83, 80, 0.2); color: #ef5350; }
.moneyness { font-size: 11px; font-weight: 500; }
.moneyness.itm { color: #66bb6a; }
.moneyness.otm { color: #ef5350; }
.moneyness.atm { color: #ffc107; }
.chart-area { flex: 1; min-height: 300px; }
</style>
