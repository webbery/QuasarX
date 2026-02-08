<template>
    <!-- 头部 -->
    <header>
      <div class="logo">
        <i class="fas fa-chart-network"></i>
        <span>QuasarX</span>
      </div>
      <div class="header-controls">
        <button v-if="is_account" class="control-btn">
          <i class="fas fa-sync-alt"></i> 刷新数据
        </button>
        <button v-if="is_strategy" class="btn" @click="onHandleRunBacktest"
          :disabled="isBacktesting"
          :class="{ 'loading': isBacktesting }"
        >
          <i :class="isBacktesting ? 'fa-spinner fa-spin' : 'fa-sync-alt'"></i>{{ isBacktesting ? '回测中...' : '执行回测' }}
        </button>
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
        <div class="nav-item">
          <i class="fas fa-satellite"></i>
          <span>另类数据</span>
        </div>
      </div>
    </nav>

    <!-- 主内容区 -->
    <main class="main-content">
      <component :is="activeComponent" ref="dynamicComponentRef"/>
    </main>

    <!-- 右侧面板 -->
    <aside class="right-panel">
      <div v-if="is_account">
        <MarketPanel></MarketPanel>
      </div>
      <div v-else-if="is_risk">
        <RiskPanel></RiskPanel>
      </div>
      <div v-else-if="is_strategy">
        <FlowComponents></FlowComponents>
      </div>
      <div v-else-if="is_setting">
        <SettingPanel :enableOperation="useOperaion"></SettingPanel>
      </div>
      <div v-else-if="is_position">
        <TradePanel :selectedSecurity="selectedSecurity" :highlight="highlightTradePanel"></TradePanel>
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
    </footer>

    <teleport to="body">
      <LoginForm :showLoginModal="showLogin" @onStatusChange="onStatusChange" @closeLoginForm="onLoginClose">
      </LoginForm>
    </teleport>
</template>
<script setup >
import { defineProps, ref, defineEmits, onMounted, onUnmounted, computed, provide } from "vue";
import LabVue from "./components/Lab.vue";
import RiskManagerVue from "./components/RiskManager.vue";
import StrategyVue from "./components/Strategy.vue";
import MarketPanel from "./components/MarketPanel.vue";
import AccountView from "./components/AccountView.vue";
import StrategyPanel from "./components/StrategyPanel.vue";
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
import sseService from "./ts/SSEService";

// 定义视图状态常量
const VIEWS = {
  ACCOUNT: 'account',
  DESIGN_STATEGY: 'strategy',
  DATA_CENTER: 'datacenter',
  SETTING_VIEW: 'setting',
  VISUAL_VIEW: 'visual_analysis',
  POSITION_VIEW: 'position',
  RISK_VIEW: 'risk'
};
// 使用响应式状态管理当前视图
let currentView = ref(VIEWS.ACCOUNT);
const dynamicComponentRef = ref(null); // 用于引用动态组件实例
let isBacktesting = ref(false);
let selectedAccount;

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
    return RiskManagerVue;
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

// 根据当前视图计算按钮状态
let is_account = computed(() => currentView.value === VIEWS.ACCOUNT);
let is_strategy = computed(() => currentView.value === VIEWS.DESIGN_STATEGY);
let is_datacenter = computed(() => currentView.value === VIEWS.DATA_CENTER);
let is_setting = computed(() => currentView.value === VIEWS.SETTING_VIEW);
let is_visual_analysis = computed(() => currentView.value === VIEWS.VISUAL_VIEW);
let is_position = computed(()=> currentView.value=== VIEWS.POSITION_VIEW);
let is_risk = computed(() => currentView.value === VIEWS.RISK_VIEW);

const unit = 1024.0/1000000000;

const onSystemStatus = (message) => {
  const infos = message.data;
  console.info(infos)
  cpu.value = (infos['cpu'] * 100).toFixed(1);
  memUsage.value = (parseFloat(infos['mem'])*unit).toFixed(2);
  totalmem.value = (parseFloat(infos["total"])*unit).toFixed(2);
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
});

onUnmounted(() => {
  localStorage.setItem('token', '')
  window.removeEventListener('loginSuccess', onLoginSucess)
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
</style>
