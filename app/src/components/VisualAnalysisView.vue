<template>
  <div class="visual-analysis-container">
    <!-- 头部区域 -->
    <header class="header">
      <div class="controls">
        <el-select v-model="selectedAssetType" placeholder="选择资产类型" class="control-item">
          <el-option label="股票" value="stocks"></el-option>
          <el-option label="债券" value="bonds"></el-option>
          <el-option label="基金" value="funds"></el-option>
          <el-option label="期货" value="futures"></el-option>
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
            <h3>板块流入流出Top5</h3>
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
            <h3>板块资金流入情况</h3>
            <div class="chart-controls">
              <el-radio-group v-model="fundsChartType" size="small">
                <el-radio-button label="radar">雷达图</el-radio-button>
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
            <h3>资产相关性热力图</h3>
            <div class="chart-controls">
              <el-button size="small" @click="exportHeatmap">导出</el-button>
            </div>
          </div>
          <div class="chart" ref="heatmapChart"></div>
        </div>
        
        <div class="chart-container">
          <div class="chart-header">
            <h3>资产聚类分析</h3>
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
        <div class="stat-card">
          <div class="stat-value">+5.2%</div>
          <div class="stat-label">平均收益率</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">12.8%</div>
          <div class="stat-label">最大回撤</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">0.87</div>
          <div class="stat-label">夏普比率</div>
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

// 响应式数据
const selectedAssetType = ref('stocks')
const dateRange = ref([])
const fundsChartType = ref('radar')
const flowDirection = ref('all') // 流入流出显示选项
const clusterCount = ref(4)

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

// 模拟数据
const generateFundsData = () => {
  const industries = ['主力', '超大单', '大单', '中单', '小单']
  const values = industries.map(() => Math.random() * 100)
  
  if (fundsChartType.value === 'radar') {
    return {
      indicator: industries.map(name => ({ name, max: 100 })),
      value: values
    }
  } else {
    return {
      industries,
      values
    }
  }
}

// 生成资金流入流出数据
const generateFlowData = async () => {
    if (!sectorData) {
        sectorData = await getSectorData()
    }
    // 取首尾5个数据
    console.info('sectorData:', sectorData)
    const inflows = sectorData.slice(0, 5)
    const outflows = sectorData.slice(-5)
    // 计算总流入流出
    console.info('in:', inflows)
    console.info('out:', outflows)
    for (let flow of inflows) {
      flow['all'] = flow['main'] + flow['supbig'] + flow['big'] + flow['mid'] + flow['small']
    }
    for (let flow of outflows) {
      flow['all'] = flow['main'] + flow['supbig'] + flow['big'] + flow['mid'] + flow['small']
    }
  
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

const generateHeatmapData = () => {
  const assets = ['AAPL', 'GOOGL', 'MSFT', 'TSLA', 'AMZN', 'META', 'NFLX', 'NVDA']
  const data = []
  for (let i = 0; i < assets.length; i++) {
    for (let j = 0; j < assets.length; j++) {
      if (i === j) {
        data.push([i, j, 1])
      } else {
        data.push([i, j, Math.random() * 2 - 1])
      }
    }
  }
  return { assets, data }
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
    updateFundsChart()
  }
  
  // 资金流入流出图表
  if (flowChart.value) {
    flowChartInstance = echarts.init(flowChart.value)
    updateFlowChart()
  }
  
  // 热力图
  if (heatmapChart.value) {
    heatmapChartInstance = echarts.init(heatmapChart.value)
    updateHeatmapChart()
  }
  
  // 聚类图
  if (clusterChart.value) {
    clusterChartInstance = echarts.init(clusterChart.value)
    updateClusterChart()
  }
}

// 更新资金流入图表
const updateFundsChart = () => {
  if (!fundsChartInstance) return
  
  const data = generateFundsData()
  
  let option
  if (fundsChartType.value === 'radar') {
    option = {
      tooltip: {
        trigger: 'item'
      },
      radar: {
        indicator: data.indicator
      },
      series: [{
        type: 'radar',
        data: [{
          value: data.value,
          name: '资金净流入',
          areaStyle: {
            color: new echarts.graphic.RadialGradient(0.5, 0.5, 1, [
              { offset: 0, color: 'rgba(64, 158, 255, 0.5)' },
              { offset: 1, color: 'rgba(64, 158, 255, 0.1)' }
            ])
          }
        }]
      }]
    }
  } else {
    option = {
      tooltip: {
        trigger: 'axis'
      },
      xAxis: {
        type: 'category',
        data: data.industries
      },
      yAxis: {
        type: 'value',
        name: '资金流入(亿)'
      },
      series: [{
        data: data.values,
        type: 'bar',
        itemStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#83bff6' },
            { offset: 0.5, color: '#188df0' },
            { offset: 1, color: '#188df0' }
          ])
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
  const values = data.map(item => parseFloat(item.value))
  
  // 设置颜色（流入为绿色，流出为红色）
  const colors = values.map(item => item['all'] >= 0 ? '#14b143' : '#ef232a')
  
  const option = {
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'shadow'
      },
      formatter: function(params) {
        const value = params[0].value
        const direction = value['all'] >= 0 ? '流入' : '流出'
        return `${params[0].name}<br/>${direction}: ${Math.abs(value['all']).toFixed(2)}亿`
      }
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true
    },
    xAxis: {
      type: 'value',
      name: '资金流(亿)',
      axisLabel: {
        formatter: function(value) {
          return Math.abs(value['all'])
        }
      }
    },
    yAxis: {
      type: 'category',
      data: names,
      axisLabel: {
        interval: 0
      }
    },
    series: [
      {
        name: '资金流', 
        type: 'bar',
        data: values.map((value, index) => ({
          value,
          itemStyle: {
            color: colors[index]
          }
        })),
        label: {
          show: true,
          position: 'right',
          formatter: function(params) {
            return Math.abs(params.value['all']).toFixed(1)
          }
        }
      }
    ]
  }
  
  flowChartInstance.setOption(option)
}

// 更新热力图
const updateHeatmapChart = () => {
  if (!heatmapChartInstance) return
  
  const { assets, data } = generateHeatmapData()
  
  const option = {
    tooltip: {
      position: 'top'
    },
    grid: {
      height: '80%',
      top: '10%'
    },
    xAxis: {
      type: 'category',
      data: assets,
      splitArea: {
        show: true
      }
    },
    yAxis: {
      type: 'category',
      data: assets,
      splitArea: {
        show: true
      }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: '0%',
      inRange: {
        color: ['#313695', '#4575b4', '#74add1', '#abd9e9', '#e0f3f8', 
                '#ffffbf', '#fee090', '#fdae61', '#f46d43', '#d73027', '#a50026']
      }
    },
    series: [{
      name: '相关性',
      type: 'heatmap',
      data: data,
      label: {
        show: true,
        formatter: function (params) {
          return params.value[2].toFixed(2)
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
    // 获取板块数据
  updateFlowChart()
//   updateFundsChart()
//   updateHeatmapChart()
//   updateClusterChart()
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
        return response.data
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
    // TODO: 初始化时间选项为今日

  initCharts()
  window.addEventListener('resize', handleResize)
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