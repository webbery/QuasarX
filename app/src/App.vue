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
        <button v-if="is_strategy" class="btn" @click="onHandleRunBacktest">
          <i class="fas fa-sync-alt"></i> 执行回测
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
          <i class="far fa-compass"></i>
          <span>模拟盘监控</span>
        </div>
        <div class="nav-item">
          <i class="fas fa-bolt"></i>
          <span>实盘监控</span>
        </div>
      </div>

      <div class="nav-section">
        <div class="nav-title">分析</div>
        <div class="nav-item">
          <i class="fas fa-flask"></i>
          <span>衍生品定价</span>
        </div>
        <div class="nav-item">
          <i class="fas fa-wind"></i>
          <span>压力测试</span>
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
        <RiskPanel></RiskPanel>
      </div>
      <div v-else-if="is_strategy">
        <FlowComponents></FlowComponents>
      </div>
      <div v-else-if="is_setting">
        <SettingPanel :enableOperation="useOperaion"></SettingPanel>
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
import { defineProps, ref, defineEmits, onMounted, onUnmounted, computed } from "vue";
import LabVue from "./components/Lab.vue";
import MineVue from "./components/Mine.vue";
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
import sseService from "./ts/SSEService";

// 定义视图状态常量
const VIEWS = {
  ACCOUNT: 'account',
  DESIGN_STATEGY: 'strategy',
  DATA_CENTER: 'datacenter',
  SETTING_VIEW: 'setting',
  VISUAL_VIEW: 'visual_analysis',
  POSITION_VIEW: 'position',
};
// 使用响应式状态管理当前视图
let currentView = ref(VIEWS.ACCOUNT);
const dynamicComponentRef = ref(null); // 用于引用动态组件实例
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

// 根据当前视图计算按钮状态
let is_account = computed(() => currentView.value === VIEWS.ACCOUNT);
let is_strategy = computed(() => currentView.value === VIEWS.DESIGN_STATEGY);
let is_datacenter = computed(() => currentView.value === VIEWS.DATA_CENTER);
let is_setting = computed(() => currentView.value === VIEWS.SETTING_VIEW);
let is_visual_analysis = computed(() => currentView.value === VIEWS.VISUAL_VIEW);
let is_position = computed(()=> currentView.value=== VIEWS.POSITION_VIEW);

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
  activeComponent.value = AccountView
  window.addEventListener('loginSuccess', onLoginSucess)
});

onUnmounted(() => {
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

const onHandleRunBacktest = () => {
  if (dynamicComponentRef.value && dynamicComponentRef.value.runBacktest) {
    dynamicComponentRef.value.runBacktest()
  }
}

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
.open-login-btn {
  color: rgb(103, 254, 141);
  border: none;
  cursor: pointer;
}
</style>
