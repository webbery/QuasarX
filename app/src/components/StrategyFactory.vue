<template>
    <div class="main-container">
        <!-- 选项卡切换 -->
        <div class="tabs">
            <button 
                class="tab-button" 
                :class="{ active: activeTab === 'flow' }"
                @click="activeTab = 'flow'"
            >
                <i class="fas fa-project-diagram"></i>
                流程图
            </button>
            <button 
                class="tab-button" 
                :class="{ active: activeTab === 'backtest' }"
                @click="activeTab = 'backtest'"
            >
                <i class="fas fa-chart-line"></i>
                回测结果
            </button>
        </div>

        <!-- 流程图面板 -->
        <div v-show="activeTab === 'flow'" class="flow-panel">
            <div class="flow-container">
                <VueFlow 
                    :nodes="nodes" 
                    :edges="edges"
                    @pane-ready="onPaneReady"
                    @drop="onDrop"
                    @dragover="onDragOver"
                >
                    <template #node-custom="nodeProps">
                        <FlowNode :node="nodeProps" 
                            @update-node="updateNodeData"
                        />
                    </template>
                    
                    <!-- 自定义连接线样式 -->
                    <template #connection-line="connectionProps">
                        <FlowConnectLine v-bind="connectionProps" />
                    </template>
                </VueFlow>
            </div>
        </div>

        <!-- 回测结果面板 -->
        <div v-show="activeTab === 'backtest'" class="backtest-panel">
            <div class="backtest-container">
                <!-- 关键指标卡片 -->
                <div class="metrics-grid">
                    <div class="metric-card">
                        <div class="metric-title">总收益</div>
                        <div class="metric-value positive">+24.5%</div>
                        <div class="metric-description">累计收益率</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-title">夏普比率</div>
                        <div class="metric-value">1.82</div>
                        <div class="metric-description">风险调整后收益</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-title">最大回撤</div>
                        <div class="metric-value negative">-8.3%</div>
                        <div class="metric-description">最大亏损幅度</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-title">胜率</div>
                        <div class="metric-value">65.2%</div>
                        <div class="metric-description">盈利交易比例</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-title">年化收益</div>
                        <div class="metric-value positive">+18.7%</div>
                        <div class="metric-description">年化收益率</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-title">交易次数</div>
                        <div class="metric-value">142</div>
                        <div class="metric-description">总交易笔数</div>
                    </div>
                </div>

                <!-- 图表区域 -->
                <div class="charts-container">
                    <!-- 价格走势与交易信号 -->
                    <div class="chart-section">
                        <div class="section-title">
                            <h3>价格走势与交易信号</h3>
                            <div class="chart-controls">
                                <button class="control-btn small">
                                    <i class="fas fa-download"></i>
                                </button>
                                <button class="control-btn small">
                                    <i class="fas fa-expand"></i>
                                </button>
                            </div>
                        </div>
                        <div class="chart-wrapper">
                            <div class="chart-legend">
                                <div class="legend-item">
                                    <div class="legend-color price-color"></div>
                                    <span>价格</span>
                                </div>
                                <div class="legend-item">
                                    <div class="legend-color buy-color"></div>
                                    <span>买入信号</span>
                                </div>
                                <div class="legend-item">
                                    <div class="legend-color sell-color"></div>
                                    <span>卖出信号</span>
                                </div>
                            </div>
                            <div class="chart-placeholder">
                                <!-- 这里可以替换为实际的图表组件，如ECharts -->
                                <div class="mock-chart">
                                    <div class="mock-chart-title">价格走势与交易信号图表</div>
                                    <div class="mock-chart-content">
                                        <div class="mock-line"></div>
                                        <div class="mock-signal buy" style="left: 20%"></div>
                                        <div class="mock-signal sell" style="left: 40%"></div>
                                        <div class="mock-signal buy" style="left: 60%"></div>
                                        <div class="mock-signal sell" style="left: 80%"></div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>

                    <!-- 收益率曲线 -->
                    <div class="chart-section">
                        <div class="section-title">
                            <h3>收益率曲线</h3>
                            <div class="chart-controls">
                                <button class="control-btn small">
                                    <i class="fas fa-download"></i>
                                </button>
                                <button class="control-btn small">
                                    <i class="fas fa-expand"></i>
                                </button>
                            </div>
                        </div>
                        <div class="chart-wrapper">
                            <div class="chart-legend">
                                <div class="legend-item">
                                    <div class="legend-color strategy-color"></div>
                                    <span>策略收益</span>
                                </div>
                                <div class="legend-item">
                                    <div class="legend-color benchmark-color"></div>
                                    <span>基准收益</span>
                                </div>
                            </div>
                            <div class="chart-placeholder">
                                <div class="mock-chart">
                                    <div class="mock-chart-title">收益率曲线图表</div>
                                    <div class="mock-chart-content">
                                        <div class="mock-line strategy-line"></div>
                                        <div class="mock-line benchmark-line"></div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>

                    <!-- 仓位变化 -->
                    <div class="chart-section">
                        <div class="section-title">
                            <h3>仓位变化</h3>
                            <div class="chart-controls">
                                <button class="control-btn small">
                                    <i class="fas fa-download"></i>
                                </button>
                                <button class="control-btn small">
                                    <i class="fas fa-expand"></i>
                                </button>
                            </div>
                        </div>
                        <div class="chart-wrapper">
                            <div class="chart-placeholder">
                                <div class="mock-chart">
                                    <div class="mock-chart-title">仓位变化图表</div>
                                    <div class="mock-chart-content">
                                        <div class="mock-position-bar"></div>
                                        <div class="mock-position-bar" style="height: 40%"></div>
                                        <div class="mock-position-bar" style="height: 80%"></div>
                                        <div class="mock-position-bar" style="height: 60%"></div>
                                        <div class="mock-position-bar" style="height: 30%"></div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useVueFlow, VueFlow, MarkerType } from '@vue-flow/core'
import FlowNode from './flow/FlowNode.vue'
import FlowConnectLine from './flow/FlowConnectLine.vue'

const { fitView } = useVueFlow()
const activeTab = ref('flow')

const keyMap = {
  "source": "来源",
  "code": "代码",
  "formula": "公式"
}

// 修正数据格式，符合VueFlow要求
const flow_data = {
  "graph": {
    "id": "graph_ma2",
    "name": "双均线动量流水线",
    "description": "包含数据输入、特征工程、信号输出和结果输出的完整流水线",
    "nodes": [
      {
        "id": "1",
        "type": "custom",
        "data": { 
          "label": "数据输入",
          "nodeType": "input",
          "params": {
            "来源": {
              "value": "股票",
              "type": "select",
              "options": ["股票", "期货"]
            },
            "代码": {
              "value": ["001038"],
              "type": "text"
            },
            "close": {
              "value": "close",
              "type": "text"
            },
            "open": {
              "value": "open",
              "type": "text"
            },
            "high": {
              "value": "high",
              "type": "text"
            },
            "low": {
              "value": "low",
              "type": "text"
            },
            "volume": {
              "value": "volume",
              "type": "text"
            }
          }
        },
        "position": { "x": 50, "y": 100 }
      },
      {
        "id": "2",
        "type": "custom",
        "data": { 
          "label": "数据预处理",
          "nodeType": "operation",
          "params": {
            "方法": {
              "value": "MA",
              "type": "select",
              "options": ["MA"]
            },
            "平滑时间": {
              "value": 5,
              "type": "text",
            }
          }
        },
        "position": { "x": 300, "y": 100 }
      },
      {
        "id": "3",
        "type": "custom",
        "data": { 
          "label": "MA_5",
          "nodeType": "operation",
          "params": {
            "方法": {
              "value": "MA",
              "type": "select",
              "options": ["MA"]
            },
            "平滑时间": {
              "value": 15,
              "type": "text",
            }
          }
        },
        "position": { "x": 550, "y": 100 }
      },
      {
        "id": "3",
        "type": "custom",
        "data": { 
          "label": "MA_15",
          "nodeType": "operation",
          "params": {
            "方法": {
              "value": "MA",
              "type": "select",
              "options": ["MA"]
            },
            "平滑时间": {
              "value": 15,
              "type": "text",
            }
          }
        },
        "position": { "x": 800, "y": 50 }
      },
      {
        "id": "6",
        "type": "custom",
        "data": { 
          "label": "MA_diff",
          "nodeType": "operation",
          "params": {
            "公式": {
              "value": "MA_5-MA_15",
              "type": "select",
              "options": ["-", "/"]
            }
          }
        },
        "position": { "x": 800, "y": 150 }
      },
      {
        "id": "5",
        "type": "custom",
        "data": { 
          "label": "结果输出",
          "nodeType": "output",
          "params": {
            "格式": {
              "value": "CSV",
              "type": "select",
              "options": ["CSV", "JSON", "数据库", "实时推送"]
            },
            "路径": {
              "value": "/data/output.csv",
              "type": "text"
            }
          }
        },
        "position": { "x": 1050, "y": 100 }
      }
    ],
    "edges": [
      {
        "id": "e1->2",
        "source": "1",
        "target": "2",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e2->3",
        "source": "1",
        "target": "3",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e3->4",
        "source": "2",
        "target": "6",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e3->6",
        "source": "3",
        "target": "6",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e4->5",
        "source": "6",
        "target": "5",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      }
    ]
  }
}

// 直接使用转换后的数据
const nodes = ref(flow_data.graph.nodes)
const edges = ref(flow_data.graph.edges)

// 拖拽放置处理
const onDrop = (event) => {
  const { dataTransfer, clientX, clientY } = event
  const nodeType = dataTransfer?.getData('application/vueflow')
  
  if (nodeType && nodeTypeConfigs[nodeType]) {
    const position = screenToFlowPosition({
      x: clientX,
      y: clientY,
    })
    
    const newNode = {
      id: `${nodeIdCounter++}`,
      type: 'custom',
      position,
      data: {
        label: nodeTypeConfigs[nodeType].label,
        nodeType: nodeTypeConfigs[nodeType].nodeType,
        params: JSON.parse(JSON.stringify(nodeTypeConfigs[nodeType].params)) // 深拷贝参数
      }
    }
    
    addNodes(newNode)
  }
}

// 拖拽悬停处理
const onDragOver = (event) => {
  event.preventDefault()
  if (event.dataTransfer) {
    event.dataTransfer.dropEffect = 'move'
  }
}

// 确保流程图在容器内居中显示
const onPaneReady = () => {
  setTimeout(() => {
    fitView({ padding: 0.2 })
  }, 100)
}

// 更新节点数据
const updateNodeData = (nodeId, paramKey, newValue) => {
  const nodeIndex = nodes.value.findIndex(node => node.id === nodeId)
  if (nodeIndex !== -1) {
    // 创建新的节点对象以触发响应式更新
    const updatedNode = {
      ...nodes.value[nodeIndex],
      data: {
        ...nodes.value[nodeIndex].data,
        params: {
          ...nodes.value[nodeIndex].data.params,
          [paramKey]: {
            ...nodes.value[nodeIndex].data.params[paramKey],
            value: newValue
          }
        }
      }
    }
    
    // 更新节点
    nodes.value[nodeIndex] = updatedNode
    
    // 如果参数变化影响其他参数的可见性，可以在这里处理
    if (paramKey === '缺失值' && newValue === '填充') {
      // 显示填充值参数
      nodes.value[nodeIndex].data.params.填充值.visible = true
    } else if (paramKey === '缺失值' && newValue !== '填充') {
      // 隐藏填充值参数
      nodes.value[nodeIndex].data.params.填充值.visible = false
    }
  }
}

const runBacktest = () => {

}
</script>
<style scoped>
.main-container {
    height: 100%;
    display: flex;
    flex-direction: column;
    background-color: var(--dark-bg);
}

/* 选项卡样式 */
.tabs {
    display: flex;
    background-color: var(--darker-bg);
    border-bottom: 1px solid var(--border);
    padding: 0 20px;
}

.tab-button {
    padding: 12px 24px;
    background: none;
    border: none;
    font-size: 14px;
    cursor: pointer;
    color: var(--text-secondary);
    border-bottom: 2px solid transparent;
    transition: all 0.3s ease;
    display: flex;
    align-items: center;
    gap: 8px;
}

.tab-button:hover {
    color: var(--text);
    background: rgba(41, 98, 255, 0.1);
}

.tab-button.active {
    color: var(--primary);
    border-bottom-color: var(--primary);
    background: rgba(41, 98, 255, 0.15);
}

/* 流程图面板 */
.flow-panel {
    flex: 1;
    display: flex;
    background-color: var(--dark-bg);
    margin: 20px;
    border-radius: 10px;
    border: 1px solid var(--border);
    overflow: hidden;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.flow-container {
    flex: 1;
    height: 100%;
    position: relative;
}

/* 自定义节点样式 */
.vue-flow__node-custom {
    padding: 10px;
    border-radius: 8px;
    border: 2px solid var(--primary);
    background: var(--panel-bg);
    box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
    min-width: 150px;
    font-size: 0.9rem;
    color: var(--text);
}

.vue-flow__node-custom.selected {
    border-color: var(--accent);
    box-shadow: 0 2px 10px rgba(255, 109, 0, 0.3);
}

.node-header {
    display: flex;
    align-items: center;
    margin-bottom: 8px;
    padding-bottom: 8px;
    border-bottom: 1px solid var(--border);
}

.node-icon {
    width: 24px;
    height: 24px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 10px;
    color: white;
    font-size: 12px;
    background-color: var(--primary);
}

.node-title {
    font-weight: 600;
    color: var(--text);
}

.node-content {
    padding: 5px 0;
}

.node-param {
    font-size: 0.8rem;
    margin: 3px 0;
    color: var(--text-secondary);
}

.vue-flow__edge-path {
    stroke: var(--border);
    stroke-width: 2;
}

.vue-flow__edge.selected .vue-flow__edge-path {
    stroke: var(--accent);
    stroke-width: 3;
}

.vue-flow__edge.animated path {
    stroke-dasharray: 5;
    animation: dashdraw 0.5s linear infinite;
}

@keyframes dashdraw {
    from { stroke-dashoffset: 10; }
}

.vue-flow__connectionline {
    z-index: 1000;
}

/* 回测结果面板 */
.backtest-panel {
    flex: 1;
    display: flex;
    padding: 20px;
    overflow-y: auto;
}

.backtest-container {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 20px;
}

/* 关键指标卡片 */
.metrics-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 16px;
}

.metric-card {
    background-color: var(--panel-bg);
    border-radius: 10px;
    padding: 20px;
    border: 1px solid var(--border);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
    text-align: center;
}

.metric-title {
    font-size: 14px;
    color: var(--text-secondary);
    margin-bottom: 8px;
}

.metric-value {
    font-size: 24px;
    font-weight: 700;
    margin-bottom: 4px;
}

.metric-value.positive {
    color: var(--secondary);
}

.metric-value.negative {
    color: #ef4444;
}

.metric-description {
    font-size: 12px;
    color: var(--text-secondary);
}

/* 图表区域 */
.charts-container {
    display: flex;
    flex-direction: column;
    gap: 20px;
}

.chart-section {
    background-color: var(--panel-bg);
    border-radius: 10px;
    padding: 20px;
    border: 1px solid var(--border);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.chart-title {
    margin-top: 0;
    margin-bottom: 16px;
    color: var(--text);
    font-size: 18px;
}

.chart-wrapper {
    position: relative;
}

.chart-legend {
    display: flex;
    gap: 16px;
    margin-bottom: 10px;
}

.legend-item {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 14px;
    color: var(--text-secondary);
}

.legend-color {
    width: 12px;
    height: 12px;
    border-radius: 2px;
}

.price-color {
    background-color: var(--primary);
}

.buy-color {
    background-color: var(--secondary);
}

.sell-color {
    background-color: #ef4444;
}

.strategy-color {
    background-color: var(--primary);
}

.benchmark-color {
    background-color: var(--text-secondary);
}

.chart-placeholder {
    height: 300px;
    background-color: var(--darker-bg);
    border-radius: 6px;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--border);
}

/* 模拟图表样式 */
.mock-chart {
    width: 100%;
    height: 100%;
    display: flex;
    flex-direction: column;
    padding: 20px;
}

.mock-chart-title {
    text-align: center;
    margin-bottom: 20px;
    color: var(--text-secondary);
    font-size: 16px;
}

.mock-chart-content {
    flex: 1;
    position: relative;
    border-bottom: 1px solid var(--border);
    border-left: 1px solid var(--border);
}

.mock-line {
    position: absolute;
    top: 30%;
    left: 0;
    right: 0;
    height: 2px;
    background-color: var(--primary);
}

.strategy-line {
    background-color: var(--primary);
    top: 40%;
}

.benchmark-line {
    background-color: var(--text-secondary);
    top: 60%;
}

.mock-signal {
    position: absolute;
    top: 0;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    transform: translateY(-50%);
}

.mock-signal.buy {
    background-color: var(--secondary);
}

.mock-signal.sell {
    background-color: #ef4444;
}

.mock-position-bar {
    position: absolute;
    bottom: 0;
    width: 18%;
    background-color: var(--accent);
    opacity: 0.7;
}

.mock-position-bar:nth-child(1) {
    left: 0;
    height: 70%;
}

.mock-position-bar:nth-child(2) {
    left: 20%;
    height: 40%;
}

.mock-position-bar:nth-child(3) {
    left: 40%;
    height: 80%;
}

.mock-position-bar:nth-child(4) {
    left: 60%;
    height: 60%;
}

.mock-position-bar:nth-child(5) {
    left: 80%;
    height: 30%;
}

/* 响应式布局 */
@media (max-width: 1200px) {
    .metrics-grid {
        grid-template-columns: repeat(2, 1fr);
    }
}

@media (max-width: 768px) {
    .metrics-grid {
        grid-template-columns: 1fr;
    }
    
    .tabs {
        padding: 0 10px;
    }
    
    .tab-button {
        padding: 10px 16px;
        font-size: 14px;
    }
}

/* 控制按钮样式 */
.control-btn.small {
    padding: 6px 10px;
    font-size: 12px;
}

.chart-controls {
    display: flex;
    gap: 8px;
}
</style>