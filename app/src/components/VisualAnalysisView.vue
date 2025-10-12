<template>
  <div class="visual-analysis-container">
    <!-- 头部区域 -->
    <header class="header">
      <div class="controls">
        <el-select v-model="selectedAssetType" placeholder="选择资产类型" class="control-item">
          <el-option label="股票板块" value="stocks"></el-option>
          <!-- <el-option label="债券" value="bonds"></el-option>
          <el-option label="基金" value="funds"></el-option>
          <el-option label="期货" value="futures"></el-option> -->
        </el-select>
        
        <el-date-picker
          v-model="dateRange"
          type="daterange"
          range-separator="至"
          start-placeholder="开始日期"
          end-placeholder="结束日期"
          class="control-item"
        >
        </el-date-picker>
        
        <el-button type="primary" @click="refreshData">刷新数据</el-button>
      </div>
    </header>
    
    <!-- 主要图表区域 -->
    <main class="main-content">
      <!-- 第一行：资金流入情况和流入流出Top5 -->
      <div class="chart-row">
        <div class="chart-container">
          <div class="chart-header">
            <h3>当日板块流入流出Top5</h3>
            <div class="chart-controls">
              <el-radio-group v-model="flowDirection" size="small">
                <el-radio-button label="all">全部</el-radio-button>
                <el-radio-button label="inflow">仅流入</el-radio-button>
                <el-radio-button label="outflow">仅流出</el-radio-button>
              </el-radio-group>
            </div>
          </div>
          <div class="chart" ref="flowChart"></div>
        </div>

        <div class="chart-container">
          <div class="chart-header">
            <h3>{{ selectedSector ? `[${selectedSector}]当日资金流向详情` : '板块资金流向详情' }}</h3>
            <div class="chart-controls">
              <el-radio-group v-model="fundsChartType" size="small">
                <!-- <el-radio-button label="radar">雷达图</el-radio-button> -->
                <el-radio-button label="bar">柱状图</el-radio-button>
              </el-radio-group>
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
              <el-button size="small" @click="exportHeatmap">导出</el-button>
            </div>
          </div>
          <div class="chart" ref="heatmapChart"></div>
        </div>
        
        <div class="chart-container">
          <div class="chart-header">
            <h3>{{ selectedSector ? `[${selectedSector}]历史资金流向` : '历史资金流向' }}</h3>
            <div class="chart-controls">
              <el-slider
                v-model="clusterCount"
                :min="2"
                :max="8"
                :step="1"
                show-stops
                size="small"
                style="width: 120px;"
              >
              </el-slider>
            </div>
          </div>
          <div class="chart" ref="clusterChart"></div>
        </div>
      </div>
    </main>
    
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
</template>

<script setup>
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import https from 'https';
import axios from 'axios'

const unit = 100000000 // 单位:亿
// 响应式数据
const selectedAssetType = ref('stocks')
const dateRange = ref([])
const fundsChartType = ref('bar')
const flowDirection = ref('all') // 流入流出显示选项
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
        flow['all'] = flow['main'] + flow['supbig'] + flow['big'] + flow['mid'] + flow['small']
      }
    }
    for (let flows of outflows) {
      for (let flow of flows.value) {
        flow['all'] = flow['main'] + flow['supbig'] + flow['big'] + flow['mid'] + flow['small']
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
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = 'https://' + server + '/v0/stocks/sector/flow?type=0'
    const agent = new https.Agent({  
        rejectUnauthorized: false // 忽略证书错误
    });
    const response = await axios.get(url, {
        httpsAgent: agent,
        responseType: 'application/json',
        headers: { 'Authorization': token}})
    console.info('response', response)
    if (response.status === 200) {
        return JSON.parse(response.data)
    }
    console.info('get sector data error')
    return null
}

// 监听数据变化
watch(fundsChartType, updateFundsChart)
watch(flowDirection, updateFlowChart)
watch(clusterCount, updateClusterChart)

// 生命周期
onMounted(() => {
  initCharts()
  window.addEventListener('resize', handleResize)
  // 初始化数据
  refreshData()
})

onUnmounted(() => {
  fundsChartInstance && fundsChartInstance.dispose()
  flowChartInstance && flowChartInstance.dispose()
  heatmapChartInstance && heatmapChartInstance.dispose()
  clusterChartInstance && clusterChartInstance.dispose()
  window.removeEventListener('resize', handleResize)
})
</script>

<style scoped>
/* 样式保持不变，与原来相同 */
.visual-analysis-container {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background-color: #f5f7fa;
  font-family: 'Helvetica Neue', Arial, sans-serif;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 24px;
  background-color: #fff;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
  z-index: 100;
}

.title {
  margin: 0;
  color: #303133;
  font-weight: 600;
}

.controls {
  display: flex;
  align-items: center;
  gap: 12px;
}

.control-item {
  width: 180px;
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
  background-color: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid #ebeef5;
}

.chart-header h3 {
  margin: 0;
  color: #303133;
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
  background-color: #fff;
  box-shadow: 0 -2px 4px rgba(0, 0, 0, 0.1);
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
  color: #303133;
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
</style>