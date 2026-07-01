<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'

const props = defineProps<{
  symbols: string[]
  correlationMatrix: number[][]
}>()

const { chartRef, initChart, updateChart } = useECharts(false)

// 模块级状态（buildOption 和事件监听共用）
let visibleMin = -1
let visibleMax = 1
let hotCounts: number[] = []

// 计算每行/每列的可见热点数量（在 visualMap 范围内）
function calcVisibleHotCounts(matrix: number[][], vmin: number, vmax: number): number[] {
  const size = matrix.length
  const counts = new Array(size).fill(0)
  for (let i = 0; i < size; i++) {
    for (let j = 0; j < size; j++) {
      if (i !== j) {
        const val = matrix[i][j]
        if (val >= vmin && val <= vmax) {
          counts[i]++
        }
      }
    }
  }
  return counts
}

function buildOption() {
  const { symbols, correlationMatrix } = props
  const n = symbols.length
  console.info('[CorrelationHeatmapChart] buildOption called:', {
    n,
    symbols: symbols.slice(0, 5),
    matrixSize: correlationMatrix?.length,
    matrixFirstRow: correlationMatrix?.[0]?.slice(0, 5)
  })
  if (n === 0 || !correlationMatrix || correlationMatrix.length !== n) {
    console.warn('[CorrelationHeatmapChart] Invalid data, returning empty option')
    return {}
  }

  // 构建热力图数据
  const data: number[][] = []
  for (let i = 0; i < n; i++) {
    if (!correlationMatrix[i] || correlationMatrix[i].length !== n) {
      console.warn('[CorrelationHeatmapChart] Matrix row length mismatch at row', i)
      return {}
    }
    for (let j = 0; j < n; j++) {
      data.push([j, i, correlationMatrix[i][j]])
    }
  }

  console.info('[CorrelationHeatmapChart] Built heatmap data:', { dataPoints: data.length })

  // 标的过多时优化显示
  const showLabels = n <= 20
  const labelRotate = n > 30 ? 60 : 45
  const labelFontSize = n > 30 ? 9 : (n > 20 ? 10 : 11)
  const displaySymbols = symbols.map(s => s.split('.')[0] || s)

  // 更新模块级状态
  visibleMin = -1
  visibleMax = 1
  hotCounts = calcVisibleHotCounts(correlationMatrix, visibleMin, visibleMax)

  // 生成 xAxis 数据（预格式化标签，带热点数量）
  const xAxisData = displaySymbols.map((name, idx) => {
    const count = hotCounts[idx] || 0
    return `${name} (${count})`
  })

  // 生成 yAxis 数据（反向，不显示数量）
  const yAxisData = [...displaySymbols].reverse()

  return createBaseChartOption({
    title: {
      text: '相关性热力图',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: {
      position: 'top',
      formatter: (p: any) => {
        const [x, y, v] = p.data
        return `${symbols[y]} ↔ ${symbols[x]}<br/>相关系数: ${v.toFixed(3)}`
      }
    },
    grid: { height: '65%', top: '12%' },
    xAxis: {
      type: 'category',
      data: xAxisData,
      axisLabel: {
        color: '#999',
        rotate: labelRotate,
        interval: 0,
        fontSize: labelFontSize
      },
      splitArea: { show: true }
    },
    yAxis: {
      type: 'category',
      data: yAxisData,
      axisLabel: {
        color: '#999',
        fontSize: labelFontSize
      },
      splitArea: { show: true }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      realtime: true,
      orient: 'vertical',
      right: 10,
      top: 'center',
      text: ['高', '低'],
      inRange: {
        color: ['#ef232a', '#1a2236', '#00c853']
      },
      textStyle: { color: '#999' }
    },
    series: [{
      name: '相关系数',
      type: 'heatmap',
      data,
      label: {
        show: showLabels,
        formatter: (p: any) => p.data[2].toFixed(2),
        fontSize: Math.max(9, labelFontSize - 1),
        color: '#e0e0e0'
      },
      emphasis: {
        itemStyle: { shadowBlur: 10, shadowColor: 'rgba(0, 0, 0, 0.5)' }
      }
    }]
  })
}

watch(() => [props.symbols, props.correlationMatrix], () => {
  console.info('[CorrelationHeatmapChart] watch triggered:', {
    symbolsLen: props.symbols.length,
    hasMatrix: !!props.correlationMatrix,
    hasChartRef: !!chartRef.value,
    shouldRender: props.symbols.length >= 2 && !!props.correlationMatrix
  })
  if (props.symbols.length >= 2 && props.correlationMatrix && chartRef.value) {
    if (!echarts.getInstanceByDom(chartRef.value)) {
      console.info('[CorrelationHeatmapChart] Initializing chart...')
      initChart()
    }
    const option = buildOption()
    console.info('[CorrelationHeatmapChart] Updating chart with option')
    updateChart(option, true)
  }
}, { immediate: true })

onMounted(() => {
  console.info('[CorrelationHeatmapChart] onMounted:', {
    symbolsLen: props.symbols.length,
    hasMatrix: !!props.correlationMatrix,
    hasChartRef: !!chartRef.value
  })
  if (chartRef.value && props.symbols.length >= 2 && props.correlationMatrix && !echarts.getInstanceByDom(chartRef.value)) {
    console.info('[CorrelationHeatmapChart] Initializing chart in onMounted...')
    initChart()
    updateChart(buildOption(), true)
  }

  // 在 onMounted 中注册事件监听
  const chart = echarts.getInstanceByDom(chartRef.value!)
  console.info('[CorrelationHeatmapChart] chart instance:', !!chart)
  if (chart) {
    chart.off('datarangeselected')
    chart.on('datarangeselected', (params: any) => {
      console.info('[CorrelationHeatmapChart] datarangeselected fired:', JSON.stringify(params))
      
      let vmin, vmax
      if (Array.isArray(params.selected)) {
        vmin = params.selected[0]
        vmax = params.selected[1]
      } else if (typeof params.selected === 'object') {
        vmin = params.selected.min ?? -1
        vmax = params.selected.max ?? 1
      } else {
        vmin = -1
        vmax = 1
      }

      console.info('[CorrelationHeatmapChart] visualMap range:', { vmin, vmax })

      visibleMin = vmin
      visibleMax = vmax
      // 根据 visualMap 范围动态计算每行可见热点数
      hotCounts = calcVisibleHotCounts(props.correlationMatrix, visibleMin, visibleMax)
      console.info('[CorrelationHeatmapChart] hotCounts (first 5):', hotCounts.slice(0, 5))

      // 生成新的 xAxis 数据（直接替换 data 数组）
      const newXAxisData = props.symbols.map((s, idx) => {
        const name = s.split('.')[0] || s
        const count = hotCounts[idx] || 0
        return `${name} (${count})`
      })

      console.info('[CorrelationHeatmapChart] newXAxisData (first 3):', newXAxisData.slice(0, 3))
      console.info('[CorrelationHeatmapChart] visualMap 更新 xAxis 标签')
      chart.setOption({
        xAxis: { data: newXAxisData }
      })
    })
    console.info('[CorrelationHeatmapChart] datarangeselected listener registered')
  }
})
</script>
