<template>
  <div class="autocorrelation-container">
    <!-- 分析信息卡片 -->
    <div v-if="data?.acf_decay" class="analysis-card">
      <div class="analysis-header">
        <span class="analysis-title">ACF 衰减分析</span>
        <span
          class="autocorrelation-badge"
          :class="{ significant: data.acf_decay.has_autocorrelation }"
        >
          {{ data.acf_decay.has_autocorrelation ? '自相关显著' : '无显著自相关' }}
        </span>
      </div>

      <div class="analysis-grid">
        <div class="analysis-item">
          <div class="label">Ljung-Box 检验</div>
          <div class="value">
            Q = {{ data.acf_decay.lb_statistic.toFixed(2) }},
            p = {{ formatPValue(data.acf_decay.lb_pvalue) }}
          </div>
        </div>

        <div class="analysis-item">
          <div class="label">衰减模式</div>
          <div class="value">
            <span :class="['mode-tag', getDecayModeClass(data.acf_decay.decay_mode)]">
              {{ getDecayModeLabel(data.acf_decay.decay_mode) }}
            </span>
            <span class="r2-info">
              (Exp R²={{ data.acf_decay.exponential_r2.toFixed(3) }},
              Hyp R²={{ data.acf_decay.hyperbolic_r2.toFixed(3) }})
            </span>
          </div>
        </div>

        <div v-if="data.acf_decay.decay_mode === 'exponential' && data.acf_decay.decay_half_life > 0" class="analysis-item">
          <div class="label">半衰期</div>
          <div class="value">{{ data.acf_decay.decay_half_life.toFixed(1) }} 天</div>
        </div>

        <div v-if="data.acf_decay.hurst_estimate > 0" class="analysis-item">
          <div class="label">Hurst 指数</div>
          <div class="value">
            H = {{ data.acf_decay.hurst_estimate.toFixed(3) }}
            <span class="hurst-note">
              {{ data.acf_decay.hurst_estimate > 0.5 ? '(长记忆/趋势持续)' : data.acf_decay.hurst_estimate < 0.5 ? '(均值回复)' : '(随机游走)' }}
            </span>
          </div>
        </div>
      </div>
    </div>

    <!-- ACF 图表 -->
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { watch, computed } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
}>()

const { chartRef, initChart, updateChart } = useECharts()

function formatPValue(p: number): string {
  if (p < 0.001) return 'p < 0.001'
  if (p < 0.01) return `p = ${p.toFixed(3)}`
  if (p < 0.05) return `p = ${p.toFixed(3)}`
  return `p = ${p.toFixed(3)}`
}

function getDecayModeClass(mode: string): string {
  switch (mode) {
    case 'exponential': return 'mode-exponential'
    case 'hyperbolic': return 'mode-hyperbolic'
    default: return 'mode-inconclusive'
  }
}

function getDecayModeLabel(mode: string): string {
  switch (mode) {
    case 'exponential': return '指数衰减 (GARCH)'
    case 'hyperbolic': return '双曲衰减 (FIGARCH)'
    default: return '无法判定'
  }
}

// 这里只渲染 |r| ACF（波动率聚集信号）
watch(() => props.data, () => {
  if (props.data?.abs_returns_acf) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) initChart()
    const ci = 1.96 / Math.sqrt(props.data.returns.length)

    const option = createBaseChartOption({
      title: { text: '|收益率| ACF（波动率聚集信号）', left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
      tooltip: { trigger: 'axis', formatter: (p: any) => `Lag ${p[0].name}<br/>ACF: ${p[0].value.toFixed(4)}` },
      grid: { left: '3%', right: '4%', bottom: '8%', top: '18%', containLabel: true },
      xAxis: {
        type: 'category',
        data: Array.from({ length: props.data.abs_returns_acf.length }, (_, i) => i),
        axisLabel: { color: '#999' }
      },
      yAxis: { type: 'value', min: -1, max: 1, axisLabel: { color: '#999' } },
      series: [{
        type: 'bar',
        data: props.data.abs_returns_acf,
        itemStyle: {
          color: (p: any) => Math.abs(p.value) > ci ? '#ff9800' : 'rgba(160, 174, 192, 0.4)'
        }
      }],
      graphic: [{
        type: 'line',
        left: 0, right: 0,
        shape: { x1: 0, y1: ci * 100 + '%', x2: '100%', y2: ci * 100 + '%' },
        style: { stroke: '#ef232a', lineWidth: 1, lineDash: [4, 4] }
      }, {
        type: 'line',
        left: 0, right: 0,
        shape: { x1: 0, y1: -ci * 100 + '%', x2: '100%', y2: -ci * 100 + '%' },
        style: { stroke: '#ef232a', lineWidth: 1, lineDash: [4, 4] }
      }]
    })

    updateChart(option, true)
  }
}, { immediate: true })
</script>

<style scoped>
.autocorrelation-container {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.analysis-card {
  background: rgba(30, 40, 60, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 6px;
  padding: 10px 14px;
  margin-bottom: 8px;
}

.analysis-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

.analysis-title {
  font-size: 13px;
  font-weight: 600;
  color: #e0e0e0;
}

.autocorrelation-badge {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 3px;
  background: rgba(76, 175, 80, 0.2);
  color: #81c784;
  border: 1px solid rgba(76, 175, 80, 0.3);
}

.autocorrelation-badge.significant {
  background: rgba(255, 152, 0, 0.2);
  color: #ffb74d;
  border-color: rgba(255, 152, 0, 0.4);
}

.analysis-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 8px;
}

.analysis-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.analysis-item .label {
  font-size: 11px;
  color: #999;
}

.analysis-item .value {
  font-size: 12px;
  color: #e0e0e0;
}

.mode-tag {
  font-size: 12px;
  font-weight: 500;
  padding: 1px 6px;
  border-radius: 3px;
}

.mode-tag.mode-exponential {
  color: #64b5f6;
  background: rgba(33, 150, 243, 0.15);
}

.mode-tag.mode-hyperbolic {
  color: #ce93d8;
  background: rgba(156, 39, 176, 0.15);
}

.mode-tag.mode-inconclusive {
  color: #999;
}

.r2-info {
  font-size: 11px;
  color: #999;
  margin-left: 4px;
}

.hurst-note {
  font-size: 11px;
  color: #999;
  margin-left: 4px;
}

.chart-container {
  flex: 1;
  min-height: 0;
}
</style>
