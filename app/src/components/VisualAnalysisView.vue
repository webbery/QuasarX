<template>
  <div class="visual-analysis-container">
    <!-- Tab 切换 -->
    <div class="main-tabs">
      <div class="tabs-header">
        <button
          v-for="tab in tabs"
          :key="tab.name"
          :class="['tab-item', { active: activeTab === tab.name }]"
          @click="activeTab = tab.name"
        >
          {{ tab.label }}
        </button>
      </div>

      <div class="tabs-content">
         <!-- 波动率分析 Tab -->
        <div v-show="activeTab === 'volatility'" class="tab-content">
          <VolatilityTab />
        </div>
        <!-- 信号分析 Tab -->
        <div v-show="activeTab === 'signal'" class="tab-content">
          <SignalTab />
        </div>
        <!-- PCA 主成分分析 Tab -->
        <div v-show="activeTab === 'pca'" class="tab-content">
          <PCATab />
        </div>
        <!-- CUSUM 结构变化检测 Tab -->
        <div v-show="activeTab === 'cusum'" class="tab-content">
          <CUSUMTab />
        </div>

        <!-- XGBoost 分析 Tab -->
        <div v-show="activeTab === 'xgboost'" class="tab-content">
          <XGBoostTab />
        </div>

        <!-- 基本面分析 Tab -->
        <div v-show="activeTab === 'fundamental'" class="tab-content">
          <FundamentalTab />
        </div>

        <!-- 资金流向 Tab -->
        <div v-show="activeTab === 'flow'" class="tab-content flow-tab">
          <!-- 头部控制区（移入资金流向 Tab） -->
          <header class="header flow-header">
            <div class="controls">
              <select v-model="selectedAssetType" class="control-item">
                <option value="stocks">股票板块</option>
              </select>

              <div class="date-range-picker">
                <input type="date" v-model="startDate" class="date-input" placeholder="开始日期" />
                <span class="date-separator">至</span>
                <input type="date" v-model="endDate" class="date-input" placeholder="结束日期" />
              </div>

              <button type="button" class="btn btn-primary" @click="refreshData">刷新数据</button>
            </div>
          </header>

          <!-- 图表区域 -->
          <div class="flow-charts">
            <!-- 第一行：资金流入情况和流入流出Top5 -->
            <div class="chart-row">
              <div class="chart-container">
                <div class="chart-header">
                  <h3>当日板块流入流出Top5</h3>
                  <div class="chart-controls">
                    <div class="radio-group">
                      <button
                        v-for="opt in flowDirectionOptions"
                        :key="opt.value"
                        :class="['radio-btn', { active: flowDirection === opt.value }]"
                        @click="flowDirection = opt.value"
                      >
                        {{ opt.label }}
                      </button>
                    </div>
                  </div>
                </div>
                <div class="chart" ref="flowChart"></div>
              </div>

              <div class="chart-container">
                <div class="chart-header">
                  <h3>{{ selectedSector ? `[${selectedSector}]当日资金流向详情` : '板块资金流向详情' }}</h3>
                  <div class="chart-controls">
                    <div class="radio-group">
                      <button
                        v-for="opt in fundsChartTypeOptions"
                        :key="opt.value"
                        :class="['radio-btn', { active: fundsChartType === opt.value }]"
                        @click="fundsChartType = opt.value"
                      >
                        {{ opt.label }}
                      </button>
                    </div>
                  </div>
                </div>
                <div class="chart" ref="fundsChart"></div>
              </div>
            </div>

            <!-- 第二行：相关性热力图和聚类分析 -->
            <div class="chart-row">
              <div class="chart-container">
                <div class="chart-header">
                  <h3>板块流入流出变化率Top5</h3>
                  <div class="chart-controls">
                    <button size="small" class="btn btn-small" @click="exportHeatmap">导出</button>
                  </div>
                </div>
                <div class="chart" ref="heatmapChart"></div>
              </div>

              <div class="chart-container">
                <div class="chart-header">
                  <h3>{{ selectedSector ? `[${selectedSector}]历史资金流向` : '历史资金流向' }}</h3>
                  <div class="chart-controls">
                    <input
                      type="range"
                      v-model.number="clusterCount"
                      :min="2"
                      :max="8"
                      :step="1"
                      class="slider"
                    />
                    <span class="slider-value">{{ clusterCount }}</span>
                  </div>
                </div>
                <div class="chart" ref="clusterChart"></div>
              </div>
            </div>

            <!-- 底部统计信息 -->
            <footer class="footer">
              <div class="stats-container">
                <div class="stat-card">
                  <div class="stat-value">¥1,245.6亿</div>
                  <div class="stat-label">总资产规模</div>
                </div>
              </div>
            </footer>
          </div>
        </div>

        <!-- 策略数据 Tab（新增） -->
        <div v-show="activeTab === 'strategyData'" class="tab-content strategy-data-tab">
          <!-- DebugNode 选择器 -->
          <div class="debug-node-selector">
            <label>选择调试节点：</label>
            <select v-model="selectedDebugNodeId" @change="onDebugNodeChange">
              <option v-for="node in availableDebugNodes" :key="node.id" :value="node.id">
                {{ node.data?.label || '调试' }} (ID: {{ node.id }})
              </option>
              <option v-if="availableDebugNodes.length === 0" disabled>无调试节点</option>
            </select>
          </div>

          <!-- 字段选择器 -->
          <div class="data-fields-selector">
            <h4>选择查看的数据字段</h4>
            <div class="field-checkboxes">
              <label v-for="field in currentDebugFields" :key="field" class="field-checkbox">
                <input type="checkbox" v-model="selectedFields" :value="field" />
                <span>{{ field }}</span>
              </label>
              <div v-if="currentDebugFields.length === 0" class="no-fields-hint">
                连接上游节点后自动显示可用字段
              </div>
            </div>
          </div>

          <!-- 数据表格 -->
          <div class="data-table-wrapper" v-if="selectedFields.length > 0 && strategyData.length > 0">
            <h4>数据预览（前 100 行）</h4>
            <table class="data-table">
              <thead>
                <tr>
                  <th class="time-col">时间</th>
                  <th v-for="field in selectedFields" :key="field" class="data-col">{{ field }}</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(row, idx) in strategyData.slice(0, 100)" :key="idx">
                  <td class="time-col">{{ row.time }}</td>
                  <td v-for="field in selectedFields" :key="field" class="data-col">
                    {{ typeof row[field] === 'number' ? row[field].toFixed(4) : (row[field] ?? '-') }}
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          <!-- 图表展示 -->
          <div class="data-charts" v-if="selectedFields.length > 0">
            <h4>数据趋势图</h4>
            <div class="chart-grid">
              <div v-for="field in selectedFields.slice(0, 6)" :key="field" class="chart-container">
                <h5>{{ field }}</h5>
                <div :ref="el => setChartRef(field, el)" class="chart"></div>
              </div>
            </div>
          </div>

          <!-- 操作按钮 -->
          <div class="action-buttons">
            <button
              class="btn btn-primary"
              @click="downloadCSV"
              :disabled="selectedFields.length === 0 || strategyData.length === 0"
            >
              <i class="fas fa-download"></i> 下载 CSV
            </button>
            <button class="btn btn-secondary" @click="returnToStrategyEditor">
              <i class="fas fa-arrow-left"></i> 返回策略编辑
            </button>
          </div>
        </div>
      </div>
    </div>


  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, watch, computed, inject } from 'vue'
import * as echarts from 'echarts'
import { fetchSectorFlow, calculateTotalFlow } from '@/lib/sectorApi'
import VolatilityTab from './volatility/VolatilityTab.vue'
import SignalTab from './signal/SignalTab.vue'
import PCATab from './pca/PCATab.vue'
import CUSUMTab from './cusum/CUSUMTab.vue'
import XGBoostTab from './xgboost/XGBoostTab.vue'
import FundamentalTab from './fundamental/FundamentalTab.vue'

const unit = 100000000 // 单位:亿

// Tab 配置
const tabs = [
  { label: '波动率分析', name: 'volatility' },
  { label: '信号分析', name: 'signal' },
  { label: 'PCA 主成分', name: 'pca' },
  { label: 'CUSUM 结构变化', name: 'cusum' },
  { label: 'XGBoost 分析', name: 'xgboost' },
  { label: '基本面分析', name: 'fundamental' },
  { label: '资金流向', name: 'flow' },
  { label: '策略数据', name: 'strategyData' }
]

// === 策略数据 Tab 状态 ===
const availableDebugNodes = ref([])
const selectedDebugNodeId = ref('')
const currentDebugFields = ref([])
const selectedFields = ref([])
const strategyData = ref([])
const chartRefs = ref({})

// Tab 切换
const activeTab = ref('volatility')

// 响应式数据
const selectedAssetType = ref('stocks')
const startDate = ref('')
const endDate = ref('')
const fundsChartType = ref('bar')
const flowDirection = ref('all') // 流入流出显示选项

const flowDirectionOptions = [
  { label: '全部', value: 'all' },
  { label: '仅流入', value: 'inflow' },
  { label: '仅流出', value: 'outflow' }
]

const fundsChartTypeOptions = [
  { label: '柱状图', value: 'bar' }
]

const clusterCount = ref(4)
const selectedSector = ref(null) // 选中的板块
const currentSectorData = ref(null) // 当前选中板块的详细数据

// 图表引用
const fundsChart = ref(null)
const flowChart = ref(null)
const heatmapChart = ref(null)
const clusterChart = ref(null)

// 图表实例
let fundsChartInstance = null
let flowChartInstance = null
let heatmapChartInstance = null
let clusterChartInstance = null

// 板块数据
let sectorData = null

// 生成资金流入流出数据
const generateFlowData = async () => {
    if (!sectorData) {
        sectorData = await getSectorData()
    }
    // 取首尾5个数据
    const inflows = sectorData.slice(0, 5)
    const outflows = sectorData.slice(-5)
    // 计算总流入流出
    for (let flows of inflows) {
      for (let flow of flows.value) {
        flow['all'] = calculateTotalFlow(flow)
      }
    }
    for (let flows of outflows) {
      for (let flow of flows.value) {
        flow['all'] = calculateTotalFlow(flow)
      }
    }
    console.info('inflows:', inflows)

  // 根据选择过滤数据
  if (flowDirection.value === 'inflow') {
    return inflows
  } else if (flowDirection.value === 'outflow') {
    return outflows
  } else {
    // 合并流入和流出，并确保总共不超过10个
    return [...inflows, ...outflows].slice(0, 10)
  }
}

// 根据选中的板块数据生成雷达图/柱状图数据
const generateFundsData = (sectorDetail = null) => {
  const categories = ['主力', '超大单', '大单', '中单', '小单']
  
  // 如果没有选中板块，显示默认数据或提示
  const item = sectorDetail.value[0]
  const values = [item.main, item.supbig, item.big, item.mid, item.small]
  console.info('sectorDetail:', values)
  if (fundsChartType.value === 'radar') {
    return {
      indicator: categories.map(name => ({ name, max: item.all })),
      value: values
    }
  } else {
    return {
      categories,
      values
    }
  }
}

// 生成板块流入流出变化率热力图数据
const generateHeatmapData = (sectorData) => {
  if (!sectorData) {
    // 如果没有数据，返回空数据
    return { sectors: [], data: [] }
  }

  // 计算每个板块的总流入流出和变化率
  const sectorsWithChangeRate = sectorData.map(sector => {
    const totalFlow = sector.main + sector.supbig + sector.big + sector.mid + sector.small
    // 这里简化处理，实际应该与前一天数据比较计算变化率
    const changeRate = Math.random() * 0.4 - 0.2 // 模拟-20%到+20%的变化率
    return {
      name: sector.name,
      totalFlow,
      changeRate
    }
  })

  // 按变化率绝对值排序，取前5个
  const top5Sectors = sectorsWithChangeRate
    .sort((a, b) => Math.abs(b.changeRate) - Math.abs(a.changeRate))
    .slice(0, 5)

  // 准备热力图数据
  const sectors = top5Sectors.map(item => item.name)
  const data = []
  
  // 假设有5个时间点
  const timePoints = ['09:30', '10:30', '11:30', '13:00', '14:00']
  
  for (let i = 0; i < sectors.length; i++) {
    for (let j = 0; j < timePoints.length; j++) {
      // 基于基础变化率生成每个时间点的变化率
      const baseRate = top5Sectors[i].changeRate
      const timeRate = baseRate * (0.8 + Math.random() * 0.4) // 在基础变化率附近波动
      data.push([j, i, timeRate])
    }
  }

  return { 
    sectors, 
    timePoints, 
    data 
  }
}

const generateClusterData = () => {
  const nodes = []
  const categories = []
  
  for (let i = 0; i < clusterCount.value; i++) {
    categories.push({ name: `类别 ${i + 1}` })
  }
  
  for (let i = 0; i < 50; i++) {
    const category = Math.floor(Math.random() * clusterCount.value)
    nodes.push({
      name: `资产 ${i + 1}`,
      category,
      value: Math.random() * 100,
      symbolSize: 10 + Math.random() * 20
    })
  }
  
  const links = []
  for (let i = 0; i < nodes.length; i++) {
    for (let j = i + 1; j < nodes.length; j++) {
      if (Math.random() < 0.1) {
        links.push({
          source: i,
          target: j
        })
      }
    }
  }
  
  return { nodes, links, categories }
}

// 初始化图表
const initCharts = () => {
  // 资金流入图表
  if (fundsChart.value) {
    fundsChartInstance = echarts.init(fundsChart.value)
    // updateFundsChart()
  }
  
  // 资金流入流出图表
  if (flowChart.value) {
    flowChartInstance = echarts.init(flowChart.value)
    // updateFlowChart()
  }
  
  // 热力图
  if (heatmapChart.value) {
    heatmapChartInstance = echarts.init(heatmapChart.value)
    // updateHeatmapChart()
  }
  
  // 聚类图
  if (clusterChart.value) {
    clusterChartInstance = echarts.init(clusterChart.value)
    // updateClusterChart()
  }
}

// 更新资金流入图表
const updateFundsChart = () => {
  if (!fundsChartInstance) return
  
  const data = generateFundsData(currentSectorData.value)
  let option
  if (fundsChartType.value === 'radar') {
    // 分离正负数据
    const positiveData = data.value.map(v => v >= 0 ? v : 0);
    const negativeData = data.value.map(v => v < 0 ? Math.abs(v) : 0);
    const absMax = Math.max(...data.value.map(Math.abs));
    const maxValue = absMax * 1.2 || 100; // 如果absMax为0，则用100
    option = {
      tooltip: {
        trigger: 'item',
        formatter: function(params) {
          const values = params.data.value;
          let total = values.reduce((prev, cur) => prev + cur, 0)
          const name = params.seriesName === '资金流入' ? '流入' : '流出';
          return `${params.name}<br/>${name}: ${(total/unit).toFixed(2)}亿`;
        }
      },
      radar: {
        indicator: data.indicator.map(ind => ({
            name: ind.name,
            min: -maxValue,
            max: maxValue
        })),
        splitNumber: 5
      },
      series: [
        {
          name: '资金流入',
          type: 'radar',
          data: [{
            value: positiveData,
            areaStyle: {
              color: new echarts.graphic.RadialGradient(0.5, 0.5, 1, [
                { offset: 0, color: 'rgba(20, 177, 67, 0.5)' },
                { offset: 1, color: 'rgba(20, 177, 67, 0.1)' }
              ])
            },
            lineStyle: {
              width: 2,
              color: '#14b143'
            },
            itemStyle: {
              color: '#14b143'
            }
          }]
        },
        {
          name: '资金流出',
          type: 'radar',
          data: [{
            value: negativeData,
            areaStyle: {
              color: new echarts.graphic.RadialGradient(0.5, 0.5, 1, [
                { offset: 0, color: 'rgba(239, 35, 42, 0.5)' },
                { offset: 1, color: 'rgba(239, 35, 42, 0.1)' }
              ])
            },
            lineStyle: {
              width: 2,
              color: '#ef232a'
            },
            itemStyle: {
              color: '#ef232a'
            }
          }]
        }
      ]
    };
  } else {
    option = {
      tooltip: {
        trigger: 'axis'
      },
      xAxis: {
        type: 'category',
        data: data.categories,
        axisLabel: {
          rotate: 45
        }
      },
      yAxis: {
        type: 'value',
        name: '资金流入(亿)',
        axisLabel: {
          formatter: function(value) {
            return (value/unit).toFixed(0)
          }
        }
      },
      series: [{
        data: data.values,
        type: 'bar',
        itemStyle: {
          color: function(params) {
            // 根据数值正负显示不同颜色
            return params.value >= 0 ? '#14b143' : '#ef232a'
          }
        },
        label: {
          show: true,
          position: 'top',
          formatter: function(params) {
            return (params.value/unit).toFixed(2)
          }
        }
      }]
    }
  }
  
  fundsChartInstance.setOption(option)
}

// 更新资金流入流出图表
const updateFlowChart = async () => {
  if (!flowChartInstance) return
  
  const data = await generateFlowData()
  
  // 准备图表数据
  const names = data.map(item => item.name)
  console.info('names:', names)
  const option = {
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'shadow'
      },
      formatter: function(params) {
        const item = params[0].data.rawData.value[0]
        const direction = item.all >= 0 ? '流入' : '流出'
        return `
          ${params[0].name}<br/>
          总${direction}: ${Math.abs(item.all/unit).toFixed(2)}亿<br/>
          主力: ${(item.main/unit).toFixed(2)}亿<br/>
          超大单: ${(item.supbig/unit).toFixed(2)}亿<br/>
          大单: ${(item.big/unit).toFixed(2)}亿<br/>
          中单: ${(item.mid/unit).toFixed(2)}亿<br/>
          小单: ${(item.small/unit).toFixed(2)}亿
        `
      }
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true
    },
    yAxis: {
      type: 'value',
      name: '资金流(亿)',
      axisLabel: {
        formatter: function(value) {
          return (value/unit).toFixed(0)
        }
      }
    },
    xAxis: {
      type: 'category',
      data: names,
      axisLabel: {
        interval: 0,
        rotate: 40
      }
    },
    series: [
      {
        name: '资金流', 
        type: 'bar',
        data: data.map(item => ({
          value: item.value[0].all,
          itemStyle: {
            color: item.value[0].all >= 0 ? '#14b143' : '#ef232a'
          },
          // 保存完整数据用于点击事件
          rawData: item
        })),
        // label: {
        //   show: true,
        //   position: 'right',
        //   formatter: function(params) {
        //     return Math.abs(params.value).toFixed(1)
        //   }
        // },
        // 添加点击事件
        emphasis: {
          focus: 'series'
        }
      }
    ]
  }
  
  flowChartInstance.setOption(option, true)
  
  // 添加点击事件监听
  flowChartInstance.off('click') // 移除旧的事件监听
  flowChartInstance.on('click', (params) => {
    if (params.componentType === 'series' && params.seriesType === 'bar') {
      const sectorData = params.data.rawData
      selectedSector.value = params.name
      currentSectorData.value = sectorData
      console.info('data:', sectorData)
      updateFundsChart() // 更新右侧图表
    }
  })
}

// 更新热力图
const updateHeatmapChart = () => {
  if (!heatmapChartInstance) return
  
  const { sectors, timePoints, data } = generateHeatmapData(sectorData)
  
  const option = {
    tooltip: {
      position: 'top',
      formatter: function(params) {
        const sector = sectors[params.data[1]]
        const time = timePoints[params.data[0]]
        const rate = (params.data[2] * 100).toFixed(2)
        return `${sector}<br/>${time}<br/>变化率: ${rate}%`
      }
    },
    grid: {
      height: '70%',
      top: '15%'
    },
    xAxis: {
      type: 'category',
      data: timePoints,
      splitArea: {
        show: true
      }
    },
    yAxis: {
      type: 'category',
      data: sectors,
      splitArea: {
        show: true
      }
    },
    visualMap: {
      min: -0.2, // -20%
      max: 0.2,  // +20%
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: '5%',
      text: ['高', '低'],
      inRange: {
        color: ['#ef232a', '#fff', '#14b143'] // 红色表示流出增加，绿色表示流入增加
      }
    },
    series: [{
      name: '变化率',
      type: 'heatmap',
      data: data,
      label: {
        show: true,
        formatter: function (params) {
          return (params.value[2] * 100).toFixed(1) + '%'
        }
      },
      emphasis: {
        itemStyle: {
          shadowBlur: 10,
          shadowColor: 'rgba(0, 0, 0, 0.5)'
        }
      }
    }]
  }
  
  heatmapChartInstance.setOption(option)
}

// 更新聚类图
const updateClusterChart = () => {
  if (!clusterChartInstance) return
  
  const { nodes, links, categories } = generateClusterData()
  
  const option = {
    tooltip: {},
    legend: [{
      data: categories.map(c => c.name)
    }],
    series: [{
      type: 'graph',
      layout: 'force',
      data: nodes,
      links: links,
      categories: categories,
      roam: true,
      label: {
        show: true,
        position: 'right',
        formatter: '{b}'
      },
      lineStyle: {
        color: 'source',
        curveness: 0.3
      },
      force: {
        repulsion: 100,
        edgeLength: 30
      }
    }]
  }
  
  clusterChartInstance.setOption(option)
}

// 响应窗口大小变化
const handleResize = () => {
  fundsChartInstance && fundsChartInstance.resize()
  flowChartInstance && flowChartInstance.resize()
  heatmapChartInstance && heatmapChartInstance.resize()
  clusterChartInstance && clusterChartInstance.resize()
}

// 刷新数据
const refreshData = async () => {
  sectorData = await getSectorData()
  updateFlowChart()
  // updateHeatmapChart()
  // 重置选中状态
  selectedSector.value = null
  currentSectorData.value = null
  // updateFundsChart()
}

// 导出热力图
const exportHeatmap = () => {
  // 在实际应用中，这里可以调用导出功能
  console.log('导出热力图数据')
}

const getSectorData = async () => {
    const data = await fetchSectorFlow(0)
    return data
}

// 监听数据变化
watch(fundsChartType, updateFundsChart)
watch(flowDirection, updateFlowChart)
watch(clusterCount, updateClusterChart)

// === 策略数据 Tab 处理函数 ===

// 注入返回策略编辑器的方法
const returnToStrategyEditor = inject('returnToStrategyEditor', () => {
  console.warn('[VisualAnalysisView] returnToStrategyEditor 未提供')
})

/**
 * 监听 load-strategy-data 事件（从 App.vue 触发）
 */
const onLoadStrategyData = (event) => {
  const { debugNodes, debugNodeId, nodes, edges } = event.detail
  
  // 保存调试节点列表
  availableDebugNodes.value = debugNodes || []
  
  // 设置默认选中的调试节点
  if (debugNodeId && debugNodes?.length > 0) {
    selectedDebugNodeId.value = debugNodeId
  } else if (debugNodes?.length > 0) {
    selectedDebugNodeId.value = debugNodes[0].id
  }
  
  // 从节点中提取可用字段（从调试节点的 outputFields 参数获取）
  updateCurrentDebugFields(nodes, edges)
  
  // 切换到策略数据 Tab
  activeTab.value = 'strategyData'
}

/**
 * 从节点配置中提取当前调试节点的可用字段
 */
const updateCurrentDebugFields = (nodes, edges) => {
  const debugNode = nodes?.find(n => n.id === selectedDebugNodeId.value)
  if (!debugNode?.data?.params?.outputFields) {
    currentDebugFields.value = []
    return
  }
  
  // 从 outputFields.options 获取可用字段
  const options = debugNode.data.params.outputFields.options || []
  currentDebugFields.value = options.map(o => o.value || o)
  
  // 默认全选
  selectedFields.value = [...currentDebugFields.value]
}

/**
 * 调试节点切换
 */
const onDebugNodeChange = () => {
  // 重新加载当前调试节点的字段
  // TODO: 当有真实数据时，需要重新获取数据
  selectedFields.value = []
  strategyData.value = []
}

/**
 * 设置图表 ref
 */
const setChartRef = (field, el) => {
  if (el) {
    chartRefs.value[field] = el
  }
}

/**
 * 下载 CSV
 */
const downloadCSV = () => {
  if (selectedFields.value.length === 0 || strategyData.value.length === 0) {
    message.warning('没有可下载的数据')
    return
  }
  
  // 构建 CSV 内容
  const headers = ['时间', ...selectedFields.value]
  const csvRows = [headers.join(',')]
  
  for (const row of strategyData.value) {
    const values = [row.time, ...selectedFields.value.map(f => {
      const val = row[f]
      return typeof val === 'number' ? val.toFixed(4) : (val ?? '')
    })]
    csvRows.push(values.join(','))
  }
  
  const csvContent = csvRows.join('\n')
  const blob = new Blob(['\ufeff' + csvContent], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = `strategy_data_${selectedDebugNodeId.value}_${new Date().toISOString().slice(0, 10)}.csv`
  link.click()
  URL.revokeObjectURL(url)
  
  message.success(`已下载 ${csvRows.length - 1} 行数据`)
}

// 从右侧面板切换 Tab
const onSwitchAnalysisTab = (e) => {
  const tab = e.detail?.tab
  if (tab) activeTab.value = tab
}

// 生命周期
onMounted(() => {
  initCharts()
  window.addEventListener('resize', handleResize)
  // 初始化数据
  refreshData()

  // 监听策略数据加载事件
  window.addEventListener('load-strategy-data', onLoadStrategyData)
  // 监听右侧面板 Tab 切换
  window.addEventListener('switch-analysis-tab', onSwitchAnalysisTab)
})

onUnmounted(() => {
  fundsChartInstance && fundsChartInstance.dispose()
  flowChartInstance && flowChartInstance.dispose()
  heatmapChartInstance && heatmapChartInstance.dispose()
  clusterChartInstance && clusterChartInstance.dispose()
  window.removeEventListener('resize', handleResize)
  window.removeEventListener('load-strategy-data', onLoadStrategyData)
  window.removeEventListener('switch-analysis-tab', onSwitchAnalysisTab)
})
</script>

<style scoped>
/* 样式保持不变，与原来相同 */
.visual-analysis-container {
  height: 90vh;
  display: flex;
  flex-direction: column;
  background-color: #1a2236;
  font-family: 'Helvetica Neue', Arial, sans-serif;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 24px;
  background-color: rgba(26, 34, 54, 0.9);
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
  z-index: 100;
}

.title {
  margin: 0;
  color: #e0e0e0;
  font-weight: 600;
}

.controls {
  display: flex;
  align-items: center;
  gap: 12px;
}

.control-item {
  padding: 6px 12px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 14px;
  outline: none;
  cursor: pointer;
}

.control-item:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.control-item option {
  background: #1a2236;
  color: #e0e0e0;
}

.date-range-picker {
  display: flex;
  align-items: center;
  gap: 8px;
}

.date-input {
  padding: 6px 12px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 14px;
  outline: none;
  cursor: pointer;
}

.date-input:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.date-input::-webkit-calendar-picker-indicator {
  filter: invert(1);
  cursor: pointer;
}

.date-separator {
  color: #999;
  font-size: 14px;
}

/* 按钮样式 */
.btn {
  padding: 6px 16px;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  background: rgba(26, 34, 54, 0.8);
  color: #e0e0e0;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
}

.btn:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.btn-primary {
  background: #2962ff;
  border-color: #2962ff;
}

.btn-primary:hover {
  background: #1e54e6;
  border-color: #1e54e6;
}

.btn-small {
  padding: 4px 12px;
  font-size: 12px;
}

/* Tab 样式 */
.main-tabs {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.tabs-header {
  display: flex;
  gap: 0;
  padding: 0 24px;
  background: rgba(26, 34, 54, 0.8);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
}

.tab-item {
  padding: 12px 24px;
  background: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  color: #999;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
}

.tab-item:hover {
  color: #e0e0e0;
}

.tab-item.active {
  color: #2962ff;
  border-bottom-color: #2962ff;
}

.tabs-content {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

/* 单选按钮组 */
.radio-group {
  display: flex;
  gap: 0;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  overflow: hidden;
}

.radio-btn {
  padding: 4px 12px;
  background: rgba(26, 34, 54, 0.8);
  border: none;
  border-right: 1px solid rgba(74, 85, 104, 0.3);
  color: #999;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
}

.radio-btn:last-child {
  border-right: none;
}

.radio-btn:hover {
  color: #e0e0e0;
}

.radio-btn.active {
  background: #2962ff;
  color: #fff;
}

/* 滑块样式 */
.chart-controls {
  display: flex;
  align-items: center;
  gap: 10px;
}

.slider {
  width: 120px;
  height: 4px;
  -webkit-appearance: none;
  appearance: none;
  background: rgba(74, 85, 104, 0.3);
  border-radius: 2px;
  outline: none;
  cursor: pointer;
}

.slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 14px;
  height: 14px;
  background: #2962ff;
  border-radius: 50%;
  cursor: pointer;
  transition: all 0.2s;
}

.slider::-webkit-slider-thumb:hover {
  background: #1e54e6;
  transform: scale(1.1);
}

.slider::-moz-range-thumb {
  width: 14px;
  height: 14px;
  background: #2962ff;
  border-radius: 50%;
  cursor: pointer;
  border: none;
  transition: all 0.2s;
}

.slider::-moz-range-thumb:hover {
  background: #1e54e6;
  transform: scale(1.1);
}

.slider-value {
  color: #e0e0e0;
  font-size: 12px;
  min-width: 20px;
  text-align: center;
}

.tab-content {
  padding: 16px;
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

/* 资金流向 Tab 样式：移除 padding，让 header 占满宽度 */
.flow-tab {
  padding: 0;
}

.flow-header {
  margin-bottom: 16px;
  flex-shrink: 0;
}

.flow-header .controls {
  padding: 16px 24px;
}

.flow-charts {
  flex: 1;
  padding: 16px;
  overflow: auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

/* 资金流向 Tab 内的图表行：移除固定高度，使用 flex 分配空间 */
.flow-charts .chart-row {
  height: auto;
  flex: 1;
  min-height: 300px;
}

/* footer 不占用 flex 空间 */
.flow-charts .footer {
  flex-shrink: 0;
  margin-top: 16px;
}

.main-content {
  flex: 1;
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  overflow: auto;
}

.chart-row {
  display: flex;
  gap: 20px;
  height: 50%;
}

.chart-container {
  flex: 1;
  background-color: rgba(26, 34, 54, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 8px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
}

.chart-header h3 {
  margin: 0;
  color: #e0e0e0;
  font-size: 16px;
  font-weight: 600;
}

.chart-controls {
  display: flex;
  align-items: center;
  gap: 10px;
}

.chart {
  flex: 1;
  min-height: 0; /* 防止图表溢出容器 */
}

.footer {
  padding: 16px 24px;
  background-color: rgba(26, 34, 54, 0.9);
  box-shadow: 0 -2px 4px rgba(0, 0, 0, 0.3);
}

.stats-container {
  display: flex;
  justify-content: space-around;
}

.stat-card {
  text-align: center;
  padding: 12px;
}

.stat-value {
  font-size: 24px;
  font-weight: 600;
  color: #e0e0e0;
  margin-bottom: 4px;
}

.stat-label {
  font-size: 14px;
  color: #909399;
}

/* 响应式设计 */
@media (max-width: 1200px) {
  .chart-row {
    flex-direction: column;
    height: auto;
  }

  .chart-container {
    min-height: 400px;
  }
}

/* === 策略数据 Tab 样式 === */
.strategy-data-tab {
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  overflow-y: auto;
}

.debug-node-selector {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  border: 1px solid var(--border);
}

.debug-node-selector label {
  font-size: 0.9rem;
  color: var(--text-secondary);
  white-space: nowrap;
}

.debug-node-selector select {
  flex: 1;
  max-width: 400px;
  padding: 8px 12px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text);
  font-size: 0.9rem;
  cursor: pointer;
}

.debug-node-selector select:focus {
  border-color: var(--primary);
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
}

.data-fields-selector h4 {
  margin: 0 0 12px 0;
  font-size: 1rem;
  color: var(--text);
}

.field-checkboxes {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  padding: 12px 16px;
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  border: 1px solid var(--border);
  max-height: 200px;
  overflow-y: auto;
}

.field-checkbox {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  font-size: 0.85rem;
  color: var(--text);
}

.field-checkbox input[type="checkbox"] {
  accent-color: var(--primary);
  width: 16px;
  height: 16px;
  cursor: pointer;
}

.field-checkbox span {
  color: var(--text-secondary);
  transition: color 0.15s ease;
}

.field-checkbox:hover span {
  color: var(--text);
}

.no-fields-hint {
  width: 100%;
  text-align: center;
  padding: 12px;
  color: var(--text-secondary);
  font-style: italic;
  font-size: 0.85rem;
}

.data-table-wrapper {
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  border: 1px solid var(--border);
  overflow-x: auto;
}

.data-table-wrapper h4 {
  margin: 0;
  padding: 12px 16px;
  font-size: 1rem;
  color: var(--text);
  border-bottom: 1px solid var(--border);
}

.data-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.85rem;
}

.data-table th {
  padding: 10px 12px;
  text-align: left;
  font-weight: 600;
  color: var(--text-secondary);
  background: rgba(0, 0, 0, 0.3);
  border-bottom: 2px solid var(--border);
  white-space: nowrap;
}

.data-table td {
  padding: 8px 12px;
  color: var(--text);
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
  font-family: 'Courier New', monospace;
  white-space: nowrap;
}

.data-table tbody tr:hover {
  background: rgba(41, 98, 255, 0.05);
}

.data-table .time-col {
  color: var(--text-secondary);
}

.data-charts h4 {
  margin: 0 0 12px 0;
  font-size: 1rem;
  color: var(--text);
}

.chart-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(400px, 1fr));
  gap: 16px;
}

.chart-grid .chart-container {
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  border: 1px solid var(--border);
  padding: 16px;
}

.chart-grid .chart-container h5 {
  margin: 0 0 12px 0;
  font-size: 0.95rem;
  color: var(--text-secondary);
}

.chart-grid .chart-container .chart {
  height: 200px;
}

.action-buttons {
  display: flex;
  gap: 12px;
  padding: 16px 0;
}

.action-buttons .btn {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  border: none;
  border-radius: 6px;
  font-size: 0.9rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
}

.action-buttons .btn-primary {
  background: linear-gradient(135deg, #10b981, #059669);
  color: white;
}

.action-buttons .btn-primary:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(16, 185, 129, 0.3);
}

.action-buttons .btn-primary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.action-buttons .btn-secondary {
  background: rgba(255, 255, 255, 0.1);
  color: var(--text);
  border: 1px solid var(--border);
}

.action-buttons .btn-secondary:hover {
  background: rgba(255, 255, 255, 0.15);
  border-color: var(--primary);
}
</style>