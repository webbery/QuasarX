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
        <button v-if="is_strategy" class="btn" @click="onHandleExportStrategy" title="导出当前策略">
          <i class="fas fa-download"></i> 导出
        </button>
        <label v-if="is_strategy" class="btn btn-file" title="导入策略文件">
          <i class="fas fa-upload"></i> 导入
          <input type="file" accept=".json" hidden @change="onHandleImportStrategy" />
        </label>
        <button v-if="is_backtest" class="btn">
          <i class="fas fa-sync-alt"></i> 部署模拟盘
        </button>
        <button v-if="is_backtest" class="control-btn">
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
        <div class="nav-item">
          <i class="fas fa-bolt"></i>
          <span>策略监控</span>
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
      <!-- 风控详情特殊处理，需要传递 props -->
      <StrategyRiskDetail
        v-if="currentView === VIEWS.RISK_DETAIL_VIEW"
        :strategy="getStrategyById(selectedStrategyId)"
        :option-data="getOptionRiskData(selectedStrategyId)"
        :stock-data="getStockRiskData(selectedStrategyId)"
        :future-data="getFutureRiskData(selectedStrategyId)"
        @back="onRiskDetailBack"
      />
      <component v-else :is="activeComponent" ref="dynamicComponentRef"
        @load-version="onLoadVersion"
        @strategy-click="onStrategyRiskClick"
      />
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
          :show-metrics-table="reportShowMetricsTable"
          @update:chart-visibility="updateReportChartVisibility"
          @update:show-metrics-table="updateReportShowMetricsTable"
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
          <FlowComponents v-else />
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
          <span>策略运行中 (3)</span>
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
      
      <!-- Live2D AI 助手按钮 -->
      <button 
        class="live2d-toggle-btn" 
        @click="live2dStore.toggle()"
        :class="{ active: live2dStore.visible }"
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
    
    <!-- Live2D AI 助手 -->
    <Live2DAssistant />
</template>
<script setup >
import { defineProps, ref, defineEmits, onMounted, onUnmounted, computed, provide, watch } from "vue";
import { message } from "./tool";
import { useHistoryStore } from './stores/history'
import { storeToRefs } from 'pinia'
import { exportStrategy, importStrategy } from './components/strategy/composables/useStrategyImportExport'
import MarketPanel from "./components/MarketPanel.vue";
import AccountView from "./components/AccountView.vue";
import RiskPanel from "./components/RiskPanel.vue";
import StrategyFactory from "./components/StrategyFactory.vue";
import FlowComponents from "./components/FlowComponents.vue";
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
import { useMockRiskData } from "./components/risk/hooks/useMockRiskData";
import sseService from "./ts/SSEService";
import Store from 'electron-store';
// 报表配置面板
import ReportConfigPanel from './components/report/ReportConfigPanel.vue';
// 知识库
import KnowledgeBaseView from './components/knowledge/KnowledgeBaseView.vue';
// Live2D AI 助手
import Live2DAssistant from './components/live2d/Live2DAssistant.vue'
import { useLive2DStore } from './stores/live2dStore'

const live2dStore = useLive2DStore()

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
};
const store = new Store()

// 风控模拟数据 hook
const {
  getStrategyById,
  getOptionRiskData,
  getStockRiskData,
  getFutureRiskData,
} = useMockRiskData()

// 使用响应式状态管理当前视图
let currentView = ref(VIEWS.ACCOUNT);
const dynamicComponentRef = ref(null); // 用于引用动态组件实例
let isBacktesting = ref(false);
let rightPanelTab = ref(localStorage.getItem('rightPanelTab') || 'components');
let selectedStrategyId = ref(null); // 当前选中的策略ID（用于风险详情）

// 监听右侧面板 Tab 变化并持久化
watch(rightPanelTab, (newTab) => {
  localStorage.setItem('rightPanelTab', newTab)
})

const servers = ref(store.get('servers') || [])
let selectedAccount;

// === 报表配置面板状态（新增） ===
const showReportConfig = ref(false)
const reportChartVisibility = ref({})
const reportShowMetricsTable = ref(true)

/**
 * 从 localStorage 加载报表配置
 */
function loadReportConfig() {
  try {
    const stored = localStorage.getItem('quasarx_report_config')
    if (stored) {
      const parsed = JSON.parse(stored)
      reportChartVisibility.value = parsed.chartVisibility || {}
      reportShowMetricsTable.value = parsed.showMetricsTable ?? true
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
      showMetricsTable: reportShowMetricsTable.value
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
 * 更新指标表格显示
 */
function updateReportShowMetricsTable(value) {
  reportShowMetricsTable.value = value
  saveReportConfig()
}

/**
 * 重置报表配置
 */
function resetReportConfig() {
  if (confirm('确定要重置报表配置为默认值吗？')) {
    localStorage.removeItem('quasarx_report_config')
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
  
  // Live2D 情境感知：首次显示时根据当前视图触发问候
  if (live2dStore.visible && live2dStore.settings.autoGreet) {
    setTimeout(() => {
      live2dStore.addGreeting()
    }, 1500)
  }
});

onUnmounted(() => {
  localStorage.setItem('token', '')
  window.removeEventListener('loginSuccess', onLoginSucess)
  window.removeEventListener('open-portfolio-manager', onHandlePortfolioMananger)
  uninitServerEvent()
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
// 报表配置相关 provide（新增）
provide('showReportConfig', showReportConfig)
provide('onShowReportConfig', onShowReportConfig)
provide('onHideReportConfig', onHideReportConfig)
provide('reportChartVisibility', reportChartVisibility)
provide('reportShowMetricsTable', reportShowMetricsTable)
provide('updateReportChartVisibility', updateReportChartVisibility)
provide('updateReportShowMetricsTable', updateReportShowMetricsTable)
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

/* Live2D 助手按钮样式 */
.live2d-toggle-btn {
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

.live2d-toggle-btn:hover {
  background: rgba(59, 130, 246, 0.2);
  border-color: rgba(59, 130, 246, 0.5);
  transform: translateY(-1px);
}

.live2d-toggle-btn.active {
  background: rgba(59, 130, 246, 0.3);
  border-color: #3b82f6;
  box-shadow: 0 0 12px rgba(59, 130, 246, 0.4);
}

.live2d-toggle-btn i {
  font-size: 14px;
}
</style>
