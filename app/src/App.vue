<template>
    <!-- 头部 -->
    <header>
      <div class="logo">
        <i class="fas fa-chart-network"></i>
        <span>QuasarX</span>
      </div>
      <div class="header-controls">
        <button v-if="is_strategy" class="btn" @click="onHandleRunBacktest"
          :disabled="isBacktesting"
          :class="{ 'loading': isBacktesting }"
        >
          <i :class="isBacktesting ? 'fa-spinner fa-spin' : 'fa-sync-alt'"></i>{{ isBacktesting ? '回测中...' : '执行回测' }}
        </button>
        
        <!-- 部署/停止/启动 按钮（根据服务端策略状态动态显示，回测模式或未登录时不显示） -->
        <template v-if="is_strategy && currentStrategyName && !isBacktestMode && isLoggedIn">
          <button v-if="!hasServerStrategy" class="btn" @click="onHandleDeploy" :disabled="isDeploying">
            <i :class="isDeploying ? 'fa-spinner fa-spin' : 'fa-cloud-upload-alt'"></i>
            {{ isDeploying ? '部署中...' : '部署' }}
          </button>
          <button v-else-if="isRunningOnServer" class="btn btn-danger" @click="onHandleStopStrategy" :disabled="isStopping">
            <i class="fas fa-stop"></i> 停止
          </button>
          <button v-else-if="isStoppedOnServer" class="btn btn-success" @click="onHandleStartStrategy" :disabled="isStarting">
            <i class="fas fa-play"></i> 启动
          </button>
        </template>
        <button v-if="is_strategy" class="control-btn" @click="onHandleExportStrategy" title="导出当前策略">
          <i class="fas fa-download"></i> 导出策略
        </button>
        <label v-if="is_strategy" class="control-btn btn-file" title="导入策略文件">
          <i class="fas fa-upload"></i> 导入策略
          <input type="file" accept=".json" hidden @change="onHandleImportStrategy" />
        </label>
        <button v-if="is_strategy" class="control-btn">
          <i class="fas fa-cloud-upload-alt"></i> 导出报告
        </button>
        <select v-if="is_position" class="form-control" style="width: auto;" v-model="selectedAccount">
          <option value="main">主交易账户</option>
          <option value="backup">备用交易账户</option>
        </select>
        <button class="control-btn" @click="onHandleSetting"><i class="fas fa-cog"></i> 设置</button>
      </div>
    </header>

    <!-- 左侧导航 -->
    <nav class="sidebar">
      <div class="nav-section">
        <div class="nav-title">交易</div>
        <div class="nav-item" :class="{ active: is_account }"
          @click="onHandleAccount"
        >
          <i class="fas fa-home"></i>
          <span>账户总览</span>
        </div>
        <div class="nav-item"  :class="{ active: is_position }"
          @click="onHandlePosition"
        >
          <i class="fas fa-boxes"></i>
          <span>持仓管理</span>
        </div>
        <div class="nav-item">
          <i class="fas fa-history"></i>
          <span>历史交易</span>
        </div>
      </div>

      <div class="nav-section">
        <div class="nav-title">策略</div>
        <div class="nav-item" :class="{ active: is_strategy }"
          @click="onHandleDesignStrategy" 
        >
          <i class="fas fa-industry"></i>
          <span>策略工厂</span>
        </div>
        <div class="nav-item" @click="onHandleStrategyTracker" :class="{ active: is_strategy_tracker }">
          <i class="fas fa-bolt"></i>
          <span>策略追踪</span>
        </div>
      </div>

      <div class="nav-section">
        <div class="nav-title">分析</div>
        <div class="nav-item">
          <i class="fas fa-flask"></i>
          <span>衍生品定价</span>
        </div>
        <div class="nav-item" :class="{active: is_risk}"
          @click="onHandleRiskMananger"
        >
          <i class="fas fa-wind"></i>
          <span>风险控制</span>
        </div>
        <div class="nav-item" :class="{active: is_portfolio}"
          @click="onHandlePortfolioMananger"
        >
          <i class="fas fa-wind"></i>
          <span>投资组合</span>
        </div>
        <div class="nav-item" :class="{ active: is_visual_analysis }"
          @click="onHandleVisualAnanlysis"
        >
          <i class="fas fa-area-chart"></i>
          <span>可视化分析</span>
        </div>
      </div>

      <div class="nav-section">
        <div class="nav-title">数据</div>
        <div class="nav-item" @click="onHandleDataCenter"  :class="{ active: is_datacenter }">
          <i class="fas fa-database"></i>
          <span>数据中心</span>
        </div>
        <!-- <div class="nav-item">
          <i class="fas fa-satellite"></i>
          <span>另类数据</span>
        </div> -->
        <div class="nav-item" :class="{ active: is_knowledgebase }" @click="onHandleKnowledgeBase">
          <i class="fas fa-satellite"></i>
          <span>知识库</span>
        </div>
      </div>
    </nav>

    <!-- 主内容区 -->
    <main class="main-content">
      <!-- 风控详情（待后端 API 实现后替换 props） -->
      <StrategyRiskDetail
        v-if="currentView === VIEWS.RISK_DETAIL_VIEW"
        :strategy="undefined"
        :option-data="undefined"
        :stock-data="undefined"
        :future-data="undefined"
        @back="onRiskDetailBack"
      />
      <KeepAlive>
        <component :is="activeComponent" ref="dynamicComponentRef"
          @load-version="onLoadVersion"
          @strategy-click="onStrategyRiskClick"
        />
      </KeepAlive>
    </main>

    <!-- 右侧面板 -->
    <aside class="right-panel">
      <div v-if="is_account">
        <MarketPanel></MarketPanel>
      </div>
      <div v-else-if="is_risk">
        <RiskPanel></RiskPanel>
      </div>
      <div v-else-if="is_strategy && showReportConfig" style="height: 100%">
        <ReportConfigPanel
          :chart-visibility="reportChartVisibility"
          @update:chart-visibility="updateReportChartVisibility"
          @reset="resetReportConfig"
        />
      </div>
      <div v-else-if="is_strategy" style="height: 100%">
        <RightPanelTabs v-model="rightPanelTab" />
        <div class="right-panel-content">
          <StrategyPanel 
            v-if="rightPanelTab === 'strategy'" 
            @load="onLoadVersionFromPanel" 
            @createNewVersion="onCreateNewVersion" 
            @delete-strategy="onDeleteStrategy" 
            @delete-version="onDeleteVersion"
          />
          <FlowComponents
            v-else
            ref="flowComponentsRef"
            @visualize-debug="onVisualizeDebug"
          />
        </div>
      </div>
      <div v-else-if="is_setting">
        <SettingPanel :enableOperation="useOperaion"></SettingPanel>
      </div>
      <div v-else-if="is_position">
        <TradePanel :selectedSecurity="selectedSecurity" :highlight="highlightTradePanel"></TradePanel>
      </div>
      <div v-else-if="is_portfolio">
        <PortfolioPanel :selectedSecurity="selectedSecurity" :highlight="highlightTradePanel"></PortfolioPanel>
      </div>
      <div v-else-if="is_visual_analysis">
        <AnalysisRightPanel />
      </div>
    </aside>

    <!-- 页脚 -->
    <footer>
      <div>QuasarX v0.0.1 | <span class="open-login-btn" @click="onLoginSwitch">{{ runningMode }}</span></div>
      <div class="status-indicators">
        <div class="status-item">
          <div class="status-dot"></div>
          <span>行情连接正常</span>
        </div>
        <div class="status-item">
          <div class="status-dot"></div>
          <span>策略运行中 ({{ runningCount }}/{{ totalCount }})</span>
        </div>
        <div class="status-item">
          <i class="fas fa-microchip"></i>
          <span>CPU: {{ cpu }}%</span>
        </div>
        <div class="status-item">
          <i class="fas fa-memory"></i>
          <span>内存: {{memUsage}}G/{{totalmem}}G</span>
        </div>
      </div>

      <!-- 模型下载进度（仅下载中时显示） -->
      <div v-if="modelLoadStatus && (modelLoadStatus.type === 'downloading' || modelLoadStatus.type === 'progress')"
           class="model-progress-inline">
        <i class="fas fa-download"></i>
        <span class="model-progress-text">{{ modelLoadStatus.text || '下载中...' }}</span>
        <el-progress
          v-if="modelLoadStatus.progress !== undefined"
          :percentage="modelLoadStatus.progress"
          :stroke-width="4"
          :show-text="false"
          class="inline-progress-bar"
        />
      </div>

      <!-- AI 助手按钮 -->
      <button
        class="chat-toggle-btn"
        @click="chatStore.toggle()"
        :class="{ active: chatStore.visible }"
        title="AI 助手"
      >
        <i class="fas fa-robot"></i>
        <span>助手</span>
      </button>
    </footer>

    <teleport to="body">
      <LoginForm :showLoginModal="showLogin" @onStatusChange="onStatusChange" @closeLoginForm="onLoginClose">
      </LoginForm>
    </teleport>
    
    <!-- AI 聊天框 -->
    <ChatBox />
</template>
<script setup >
import { defineProps, ref, defineEmits, onMounted, onUnmounted, computed, provide, watch } from "vue";
import axios from 'axios';
import { message } from "./tool";
import { useHistoryStore } from './stores/history'
import { storeToRefs } from 'pinia'
import { exportStrategy, importStrategy } from './components/strategy/composables/useStrategyImportExport'
import MarketPanel from "./components/MarketPanel.vue";
import AccountView from "./components/AccountView.vue";
import RiskPanel from "./components/RiskPanel.vue";
import StrategyFactory from "./components/StrategyFactory.vue";
import StrategyTracker from "./components/StrategyTracker.vue";
import FlowComponents from "./components/FlowComponents.vue";
import { getAllDebugNodes } from '@/lib/nodes/useDebugNodeFields'
import DataCenter from "./components/DataCenter.vue";
import LoginForm from "./components/LoginForm.vue";
import SettingView from "./components/SettingView.vue";
import SettingPanel from "./components/SettingPanel.vue";
import VisualAnalysisView from "./components/VisualAnalysisView.vue";
import PositionManager from "./components/PositionManager.vue";
import TradePanel from "./components/TradePanel.vue";
import StrategyPanel from "./components/StrategyPanel.vue";
import RightPanelTabs from "./components/RightPanelTabs.vue";
import PortfolioView from "./components/PortfolioView.vue";
import PortfolioPanel from "./components/PortfolioPanel.vue";
// Risk 新组件
import RiskView from "./components/risk/RiskView.vue";
import StrategyRiskDetail from "./components/risk/StrategyRiskDetail.vue";
import sseService from "./ts/SSEService";
import Store from 'electron-store';
// 报表配置面板
import AnalysisRightPanel from './components/AnalysisRightPanel.vue';
import ReportConfigPanel from './components/report/ReportConfigPanel.vue';
import { CHART_REGISTRY } from './components/report/config/chartRegistry';
// 知识库
import KnowledgeBaseView from './components/knowledge/KnowledgeBaseView.vue';
// AI 聊天框
import ChatBox from './components/ChatBox.vue'
import { useChatStore } from './stores/chatStore'

const chatStore = useChatStore()

// ---- 模型加载状态 ----
const modelLoadStatus = ref(null);

// 定义视图状态常量
const VIEWS = {
  ACCOUNT: 'account',
  DESIGN_STATEGY: 'strategy',
  DATA_CENTER: 'datacenter',
  SETTING_VIEW: 'setting',
  VISUAL_VIEW: 'visual_analysis',
  POSITION_VIEW: 'position',
  RISK_VIEW: 'risk',
  RISK_DETAIL_VIEW: 'risk_detail',
  PORTFOLIO_VIEW: 'portfolio',
  KNOWLEDGE_BASE: 'knowledge_base',
  STRATEGY_TRACKER: 'strategy_tracker',
};
const store = new Store()

// 风控模拟数据 hook（待后端 API 实现后替换）
// const { getStrategyById, getOptionRiskData, getStockRiskData, getFutureRiskData } = useMockRiskData()

// 使用响应式状态管理当前视图
let currentView = ref(VIEWS.ACCOUNT);
const dynamicComponentRef = ref(null); // 用于引用动态组件实例
const flowComponentsRef = ref(null); // 用于引用 FlowComponents（策略编辑器）
let isBacktesting = ref(false);
let rightPanelTab = ref(localStorage.getItem('rightPanelTab') || 'components');
let selectedStrategyId = ref(null); // 当前选中的策略ID（用于风险详情）

// === 策略编辑状态保存（用于视图切换后恢复） ===
const strategyEditorState = ref(null)

// 监听右侧面板 Tab 变化并持久化
watch(rightPanelTab, (newTab) => {
  localStorage.setItem('rightPanelTab', newTab)
})

const servers = ref(store.get('servers') || [])
let selectedAccount;

// === 报表配置面板状态（新增） ===
const showReportConfig = ref(false)
const reportChartVisibility = ref({})

/**
 * 从 localStorage 加载报表配置
 */
function loadReportConfig() {
  try {
    const stored = localStorage.getItem('quasarx_report_config')
    if (stored) {
      const parsed = JSON.parse(stored)
      reportChartVisibility.value = parsed.chartVisibility || {}
    }
  } catch (e) {
    console.warn('[App] 加载报表配置失败', e)
  }
}

/**
 * 保存报表配置到 localStorage
 */
function saveReportConfig() {
  try {
    const config = {
      chartVisibility: reportChartVisibility.value,
    }
    localStorage.setItem('quasarx_report_config', JSON.stringify(config))
  } catch (e) {
    console.warn('[App] 保存报表配置失败', e)
  }
}

/**
 * 显示报表配置面板
 */
function onShowReportConfig() {
  showReportConfig.value = true
}

/**
 * 隐藏报表配置面板
 */
function onHideReportConfig() {
  showReportConfig.value = false
}

/**
 * 更新图表可见性
 */
function updateReportChartVisibility(newVisibility) {
  reportChartVisibility.value = newVisibility
  saveReportConfig()
}

/**
 * 重置报表配置（含布局）
 */
function resetReportConfig() {
  if (confirm('确定要重置报表配置为默认值吗？这将重置图表可见性和顺序。')) {
    localStorage.removeItem('quasarx_report_config')
    localStorage.removeItem('quasarx_report_layout')
    loadReportConfig()
  }
}

// 根据当前视图动态计算活动组件
let activeComponent = computed(() => {
  if (currentView.value === VIEWS.ACCOUNT)
    return AccountView;
  if (currentView.value === VIEWS.DESIGN_STATEGY)
    return StrategyFactory;
  if (currentView.value === VIEWS.DATA_CENTER)
    return DataCenter;
  if (currentView.value === VIEWS.SETTING_VIEW)
    return SettingView;
  if (currentView.value === VIEWS.VISUAL_VIEW)
    return VisualAnalysisView;
  if (currentView.value === VIEWS.POSITION_VIEW)
    return PositionManager;
  if (currentView.value === VIEWS.RISK_VIEW)
    return RiskView;
  if (currentView.value === VIEWS.RISK_DETAIL_VIEW)
    return StrategyRiskDetail;
  if (currentView.value === VIEWS.PORTFOLIO_VIEW)
    return PortfolioView;
  if (currentView.value === VIEWS.KNOWLEDGE_BASE)
    return KnowledgeBaseView;
  if (currentView.value === VIEWS.STRATEGY_TRACKER)
    return StrategyTracker;
  return AccountView;
});

let activeIndex = ref(0);
let runningMode = ref('登录')
let showLogin = ref(false)
let cpu = ref("0")
let memUsage = ref("0")
let totalmem = ref("0")
// 1-展示已添加的服务器 2-展示已添加的交易所 3-新添加一个服务器 4-新添加一个交易所
let useOperaion = ref(0)
// 持仓证券的信息
const selectedSecurity = ref(null)
const highlightTradePanel = ref(false)    // 高亮交易面板
// 投资组合相关
const currentStrategyId = ref('')  // 当前策略图 ID
const currentPortfolioConfig = ref(null)  // 当前选中的组合配置
const portfolioConfigs = ref([])  // 组合配置列表

// 根据当前视图计算按钮状态
let is_account = computed(() => currentView.value === VIEWS.ACCOUNT);
let is_strategy = computed(() => currentView.value === VIEWS.DESIGN_STATEGY);
let is_datacenter = computed(() => currentView.value === VIEWS.DATA_CENTER);
let is_setting = computed(() => currentView.value === VIEWS.SETTING_VIEW);
let is_visual_analysis = computed(() => currentView.value === VIEWS.VISUAL_VIEW);
let is_position = computed(()=> currentView.value=== VIEWS.POSITION_VIEW);
let is_risk = computed(() => currentView.value === VIEWS.RISK_VIEW || currentView.value === VIEWS.RISK_DETAIL_VIEW);
let is_portfolio = computed(() => currentView.value === VIEWS.PORTFOLIO_VIEW);
let is_knowledgebase = computed(() => currentView.value === VIEWS.KNOWLEDGE_BASE);
let is_strategy_tracker = computed(() => currentView.value === VIEWS.STRATEGY_TRACKER);

/** 是否为回测模式（回测模式下不显示部署/启动/停止按钮） */
const isBacktestMode = computed(() => runningMode.value.includes('回测'));

/** 是否已登录（有 token 表示已登录） */
const isLoggedIn = computed(() => {
  const token = localStorage.getItem('token')
  return token && token.length > 0
});

const unit = 1024.0/1000000000;

const onSystemStatus = (message) => {
  const infos = message.data;
  console.info(infos)
  cpu.value = (infos['cpu'] * 100).toFixed(1);
  memUsage.value = (parseFloat(infos['mem'])*unit).toFixed(2);
  totalmem.value = (parseFloat(infos["total"])*unit).toFixed(2);
}

const addServer = (server) => {
  servers.value.push(server)
  store.set('servers', servers.value) // 同步到持久化存储
}

const deleteServer = (serverName) => {
  servers.value = servers.value.filter(s => s.name !== serverName)
  store.set('servers', servers.value)
}

const updateServer = (oldName, newData) => {
  const index = servers.value.findIndex(s => s.name === oldName)
  if (index !== -1) {
    servers.value[index] = { ...servers.value[index], ...newData }
    store.set('servers', servers.value)
  }
}

const onLoginSucess = () => {
  // 服务器状态监控
  initServerEvent();
}

const initServerEvent = () => {
  sseService.on('system_status', onSystemStatus)
}

const uninitServerEvent = () => {
  sseService.off('system_status', onSystemStatus)
}

// 从策略面板加载版本
const onLoadVersionFromPanel = (versionId) => {
  console.info('onLoadVersionFromPanel:', versionId)
  // 调用 StrategyFactory 的 loadVersionFromHistory 方法
  if (dynamicComponentRef.value && dynamicComponentRef.value.loadVersionFromHistory) {
    dynamicComponentRef.value.loadVersionFromHistory(versionId)
  }
}

// 创建新版本（从策略面板）
const onCreateNewVersion = (strategyId) => {
  console.info('onCreateNewVersion:', strategyId)
  // 可以在这里传递 strategyId 给 StrategyFactory
}

// 删除策略后，清空画布（如果当前显示的正是被删除的策略）
const onDeleteStrategy = (strategyId) => {
  if (dynamicComponentRef.value && dynamicComponentRef.value.clearCanvasIfStrategyMatches) {
    dynamicComponentRef.value.clearCanvasIfStrategyMatches(strategyId)
  }
}

// 删除版本后，清空画布（如果当前显示的正是被删除的版本）
const onDeleteVersion = (versionId) => {
  if (dynamicComponentRef.value && dynamicComponentRef.value.clearCanvasIfVersionMatches) {
    dynamicComponentRef.value.clearCanvasIfVersionMatches(versionId)
  }
}

onMounted(() => {
  window.addEventListener('loginSuccess', onLoginSucess)
  const remotes = localStorage.getItem('remote')
  if (remotes) { // 已经设置过服务器
    activeComponent.value = AccountView
    const token = localStorage.getItem('token')
    console.info('token:', token)
    if (!token || token.length == 0) {
      showLogin.value = true
    }
  } else {
    activeComponent.value = SettingView
  }

  // 加载报表配置
  loadReportConfig()

  // 监听打开配置管理器事件
  window.addEventListener('open-portfolio-manager', onHandlePortfolioMananger)

  // 监听嵌入模型加载状态
  window.addEventListener('message', (event) => {
    if (event.data?.payload === 'model-load-status') {
      modelLoadStatus.value = event.data.data;
      console.info('modelLoadStatus.value:', modelLoadStatus.value)
      // ready 或 error 后，3秒自动隐藏
      if (event.data.data?.type === 'ready' || event.data.data?.type === 'error') {
        setTimeout(() => {
          if (modelLoadStatus.value?.type === event.data.data?.type) {
            modelLoadStatus.value = null;
          }
        }, 3000);
      }
    }
  });

  // 初始化 history store（加载策略和版本列表）
  historyStore.initialize()

  // 启动服务端策略状态轮询
  fetchServerStrategies()
  _strategyTimer = setInterval(fetchServerStrategies, 10000)

  // 监听策略可视化调试事件（从 FlowComponents 触发）
  window.addEventListener('visualize-strategy', onVisualizeStrategy)
})

onUnmounted(() => {
  localStorage.setItem('token', '')
  window.removeEventListener('loginSuccess', onLoginSucess)
  window.removeEventListener('open-portfolio-manager', onHandlePortfolioMananger)
  window.removeEventListener('visualize-strategy', onVisualizeStrategy)
  uninitServerEvent()
  if (_strategyTimer) {
    clearInterval(_strategyTimer)
    _strategyTimer = null
  }
})

const emits = defineEmits(["refush", "onSettingChanged"]);

const onHandleDesignStrategy = () => {
  console.info("onHandleDesignStrategy");
  currentView.value = VIEWS.DESIGN_STATEGY;
}

const onHandleAccount = () => {
  console.info("onHandleAccount");
  currentView.value = VIEWS.ACCOUNT;
};

const onHandleDataCenter = () => {
  console.info("onHandleDataCenter");
  currentView.value = VIEWS.DATA_CENTER;
}

const onHandleVisualAnanlysis = () => {
  currentView.value = VIEWS.VISUAL_VIEW;
}

/**
 * 调试节点可视化按钮点击：保存策略状态并切换到可视化面板
 */
const onVisualizeDebug = (debugNodeId) => {
  if (flowComponentsRef.value?.onVisualizeDebug) {
    flowComponentsRef.value.onVisualizeDebug(debugNodeId)
  }
}

/**
 * 接收 FlowComponents 发来的可视化事件
 */
const onVisualizeStrategy = (event) => {
  const { debugNodeId, debugNodes, nodes, edges, strategyId, versionId } = event.detail
  
  // 保存当前策略编辑状态
  strategyEditorState.value = {
    nodes,
    edges,
    strategyId,
    versionId,
  }
  
  // 切换到可视化分析面板
  currentView.value = VIEWS.VISUAL_VIEW
  
  // 通知可视化面板加载数据
  window.dispatchEvent(new CustomEvent('load-strategy-data', {
    detail: {
      debugNodeId,
      debugNodes,
      nodes,
      edges,
    }
  }))
  
  message.success('已切换到可视化分析面板')
}

/**
 * 从可视化面板返回策略编辑器
 */
const returnToStrategyEditor = () => {
  if (strategyEditorState.value) {
    // 恢复策略编辑状态
    currentView.value = VIEWS.DESIGN_STATEGY
    
    message.success('已返回策略编辑器')
  } else {
    currentView.value = VIEWS.DESIGN_STATEGY
  }
}

const onHandleRiskMananger = () => {
  currentView.value = VIEWS.RISK_VIEW
  selectedStrategyId.value = null
}

const onRiskDetailBack = () => {
  currentView.value = VIEWS.RISK_VIEW
  selectedStrategyId.value = null
}

const onStrategyRiskClick = (strategy) => {
  selectedStrategyId.value = strategy.id
  currentView.value = VIEWS.RISK_DETAIL_VIEW
}

const onHandlePortfolioMananger = () => {
  currentView.value = VIEWS.PORTFOLIO_VIEW;
}

const onHandleKnowledgeBase = () => {
  currentView.value = VIEWS.KNOWLEDGE_BASE;
}

const onHandlePosition = () => {
  currentView.value = VIEWS.POSITION_VIEW;
}

const onHandleStrategyTracker = () => {
  currentView.value = VIEWS.STRATEGY_TRACKER;
}
const onLoginSwitch = () => {
  showLogin.value = !showLogin.value
}

const onStatusChange = (status, info) => {
  console.info('onStatusChange:', status, info)
  if (status) {
    runningMode.value = info
    showLogin.value = false
  }
}

const onLoginClose = () => {
    showLogin.value = false
}

const onHandleSetting = () => {
  currentView.value = VIEWS.SETTING_VIEW;
}

const onHandleRunBacktest = async () => {
  if (!isBacktesting.value && dynamicComponentRef.value && dynamicComponentRef.value.runBacktest) {
    isBacktesting.value = true;
    try {
      await dynamicComponentRef.value.runBacktest()
    } catch (error) {
      console.error('回测执行失败:', error);
    }
    isBacktesting.value = false;
  }
}

// === 策略导入导出 ===
const historyStore = useHistoryStore()
const { strategies, versions } = storeToRefs(historyStore)

// === 服务端策略状态 ===
const serverStrategies = ref([])  // [{name, running}, ...]
const isDeploying = ref(false)
const isStopping = ref(false)
const isStarting = ref(false)
let _strategyTimer = null

/** 当前画布策略名（从 IndexedDB 获取） */
const currentStrategyName = computed(() => {
  if (!currentStrategyId.value) return ''
  const s = strategies.value.find(s => s.id === currentStrategyId.value)
  return s?.name || ''
})

/** 服务端是否存在该策略 */
const hasServerStrategy = computed(() =>
  currentStrategyName.value
    ? serverStrategies.value.some(s => s.name === currentStrategyName.value)
    : false
)

/** 策略是否在服务端运行中 */
const isRunningOnServer = computed(() =>
  currentStrategyName.value
    ? serverStrategies.value.some(s => s.name === currentStrategyName.value && s.running)
    : false
)

/** 策略是否已部署但停止 */
const isStoppedOnServer = computed(() =>
  hasServerStrategy.value && !isRunningOnServer.value
)

/** 运行中的策略数量 */
const runningCount = computed(() =>
  serverStrategies.value.filter(s => s.running).length
)

/** 策略总数 */
const totalCount = computed(() =>
  serverStrategies.value.length
)

/** 从服务端获取策略状态 */
const fetchServerStrategies = async () => {
  try {
    const res = await axios.get('/v0/strategy')
    serverStrategies.value = Array.isArray(res.data) ? res.data : (res.data.strategies || [])
  } catch (e) {
    console.warn('获取策略状态失败', e)
  }
}

/** 部署策略 */
const onHandleDeploy = async () => {
  if (!currentStrategyName.value || !dynamicComponentRef.value?.getStrategyGraph) {
    message.warning('请先选择一个策略')
    return
  }
  isDeploying.value = true
  try {
    const graph = dynamicComponentRef.value.getStrategyGraph()
    const res = await axios.post('/v0/strategy', {
      mode: 0,
      name: currentStrategyName.value,
      script: graph
    })
    if (res.data.message === 'success') {
      message.success(`策略 "${currentStrategyName.value}" 已部署`)
      await fetchServerStrategies()
    }
  } catch (e) {
    message.error('部署失败: ' + (e.response?.data?.message || e.message))
  } finally {
    isDeploying.value = false
  }
}

/** 停止策略 */
const onHandleStopStrategy = async () => {
  if (!currentStrategyName.value) return
  isStopping.value = true
  try {
    await axios.post('/v0/strategy', { mode: 2, name: currentStrategyName.value })
    message.info(`策略 "${currentStrategyName.value}" 已停止`)
    await fetchServerStrategies()
  } catch (e) {
    message.error('停止失败: ' + e.message)
  } finally {
    isStopping.value = false
  }
}

/** 启动策略 */
const onHandleStartStrategy = async () => {
  if (!currentStrategyName.value) return
  isStarting.value = true
  try {
    await axios.post('/v0/strategy', { mode: 1, name: currentStrategyName.value })
    message.success(`策略 "${currentStrategyName.value}" 已启动`)
    await fetchServerStrategies()
  } catch (e) {
    message.error('启动失败: ' + e.message)
  } finally {
    isStarting.value = false
  }
}

/**
 * 导出当前策略
 */
const onHandleExportStrategy = async () => {
  if (!selectedStrategyId.value) {
    message.warning('请先选择一个策略')
    return
  }
  await exportStrategy(selectedStrategyId.value, strategies.value, versions.value, historyStore)
}

/**
 * 导入策略文件
 */
const onHandleImportStrategy = async (event) => {
  const file = event.target.files?.[0]
  if (!file) return

  // 重置 input 以便可以重复选择同一文件
  event.target.value = ''

  const newStrategyId = await importStrategy(file, historyStore)
  if (newStrategyId) {
    // 导入成功，选中新导入的策略
    selectedStrategyId.value = newStrategyId
  }
}

const handleSecuritySelection = (securityData) => {
    selectedSecurity.value = securityData;
    highlightTradePanel.value = true;
    
    // 3秒后取消高亮
    setTimeout(() => {
        highlightTradePanel.value = false;
    }, 2000);
};

provide('handleSecuritySelection', handleSecuritySelection)
provide('highlightTradePanel', highlightTradePanel)
provide('servers', servers)
provide('addServer', addServer)
provide('deleteServer', deleteServer)
provide('updateServer', updateServer)
// 投资组合相关 provide
provide('currentStrategyId', currentStrategyId)
provide('currentPortfolioConfig', currentPortfolioConfig)
provide('portfolioConfigs', portfolioConfigs)

// === 策略状态共享（供 StrategyTracker 实时监控使用） ===
provide('serverStrategies', serverStrategies)
provide('fetchServerStrategies', fetchServerStrategies)
// 报表配置相关 provide（新增）
provide('showReportConfig', showReportConfig)
provide('onShowReportConfig', onShowReportConfig)
provide('onHideReportConfig', onHideReportConfig)
provide('reportChartVisibility', reportChartVisibility)
provide('updateReportChartVisibility', updateReportChartVisibility)
provide('returnToStrategyEditor', returnToStrategyEditor)
</script>

<style scoped>
.btn {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border: none;
  padding: 12px 25px;
  border-radius: 8px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.3s ease;
  display: flex;
  align-items: center;
  gap: 8px;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}
.btn:hover:not(:disabled) {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  box-shadow: 0 6px 12px rgba(0, 0, 0, 0.15);
  transform: translateY(-2px);
}

/* 文件导入按钮样式 */
.btn-file {
  cursor: pointer;
  display: inline-flex;
}

/* 禁用状态样式 */
.btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
  transform: none;
  box-shadow: none;
}

/* 加载状态样式 */
.btn.loading {
  background: linear-gradient(90deg, #3b82f6, #60a5fa);
}

/* spinner动画 */
@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.fa-spinner {
  animation: spin 1s linear infinite;
}
.open-login-btn {
  color: rgb(103, 254, 141);
  border: none;
  cursor: pointer;
}

/* AI 聊天框按钮样式 */
.chat-toggle-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 12px;
  border: 1px solid rgba(59, 130, 246, 0.3);
  border-radius: 16px;
  background: rgba(59, 130, 246, 0.1);
  color: #60a5fa;
  cursor: pointer;
  font-size: 12px;
  font-weight: 500;
  transition: all 0.2s ease;
  margin-left: 12px;
}

.chat-toggle-btn:hover {
  background: rgba(59, 130, 246, 0.2);
  border-color: rgba(59, 130, 246, 0.5);
  transform: translateY(-1px);
}

.chat-toggle-btn.active {
  background: rgba(59, 130, 246, 0.3);
  border-color: #3b82f6;
  box-shadow: 0 0 12px rgba(59, 130, 246, 0.4);
}

.chat-toggle-btn i {
  font-size: 14px;
}

/* 模型下载进度（内联于 footer，助手左侧） */
.model-progress-inline {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 12px;
  background: rgba(37, 99, 235, 0.15);
  border-radius: 16px;
  border: 1px solid rgba(59, 130, 246, 0.3);
  margin-left: auto;
  margin-right: 8px;
  max-width: 300px;
  animation: fade-in-scale 0.3s ease;
}

.model-progress-inline i {
  font-size: 12px;
  color: #60a5fa;
  animation: spin 1s linear infinite;
}

.model-progress-text {
  font-size: 11px;
  color: #60a5fa;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 140px;
}

.inline-progress-bar {
  width: 80px;
  flex-shrink: 0;
}

.inline-progress-bar :deep(.el-progress-bar__outer) {
  background: rgba(255, 255, 255, 0.1);
}

.inline-progress-bar :deep(.el-progress-bar__inner) {
  background: #60a5fa;
  border-radius: 2px;
}

/* 右侧面板内容区填充 */
.right-panel-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  height: 100%;
}

.right-panel-content > * {
  flex: 1;
  min-height: 0;
}

@keyframes fade-in-scale {
  from {
    opacity: 0;
    transform: scale(0.9);
  }
  to {
    opacity: 1;
    transform: scale(1);
  }
}
</style>
