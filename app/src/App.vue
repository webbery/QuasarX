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
        <button v-if="is_backtest" class="btn" @click="onHandleRunBacktest">
          <i class="fas fa-sync-alt"></i> 执行回测
        </button>
        <button v-if="is_backtest" class="btn">
          <i class="fas fa-sync-alt"></i> 部署模拟盘
        </button>
        <button v-if="is_backtest" class="control-btn">
          <i class="fas fa-cloud-upload-alt"></i> 导出报告
        </button>
        <button class="control-btn" @click="onHandleSetting"><i class="fas fa-cog"></i> 设置</button>
      </div>
    </header>

    <!-- 左侧导航 -->
    <nav class="sidebar">
      <div class="nav-section">
        <div class="nav-title">交易</div>
        <div class="nav-item" :class="{ active: is_account }">
          <i class="fas fa-home"></i>
          <span @click="onHandleAccount">账户总览</span>
        </div>
        <div class="nav-item">
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
        <div class="nav-item" @click="onHandleDesignStrategy" :class="{ active: is_strategy }">
          <i class="fas fa-industry"></i>
          <span>策略工厂</span>
        </div>
        <div class="nav-item"  @click="onHandleBacktest"  :class="{ active: is_backtest }">
          <i class="fas fa-project-diagram"></i>
          <span>策略回测</span>
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
        <div class="nav-item">
          <i class="fas fa-chart-scatter"></i>
          <span>相关性分析</span>
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
      <div v-if="is_backtest">
        <StrategyPanel></StrategyPanel>
      </div>
      <div v-else-if="is_account">
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
          <span>CPU: {{ cpu }}</span>
        </div>
        <div class="status-item">
          <i class="fas fa-memory"></i>
          <span>内存: 3.2G/8G</span>
        </div>
      </div>
    </footer>

    <teleport to="body">
      <LoginForm :showLoginModal="showLogin" @onStatusChange="onStatusChange" @closeLoginForm="onLoginClose">
      </LoginForm>
    </teleport>
</template>
<script setup >
import { defineProps, ref, defineEmits, onMounted, computed } from "vue";
import LabVue from "./components/Lab.vue";
import MineVue from "./components/Mine.vue";
import RiskManagerVue from "./components/RiskManager.vue";
import StrategyVue from "./components/Strategy.vue";
import MarketPanel from "./components/MarketPanel.vue";
import AccountView from "./components/AccountView.vue";
import BackTestView from "./components/BackTestView.vue"
import StrategyPanel from "./components/StrategyPanel.vue";
import RiskPanel from "./components/RiskPanel.vue";
import StrategyFactory from "./components/StrategyFactory.vue";
import FlowComponents from "./components/FlowComponents.vue";
import DataCenter from "./components/DataCenter.vue";
import LoginForm from "./components/LoginForm.vue";
import SettingView from "./components/SettingView.vue";
import SettingPanel from "./components/SettingPanel.vue";

// 定义视图状态常量
const VIEWS = {
  ACCOUNT: 'account',
  BACKTEST: 'backtest',
  DESIGN_STATEGY: 'strategy',
  DATA_CENTER: 'datacenter',
  SETTING_VIEW: 'setting',
};
// 使用响应式状态管理当前视图
let currentView = ref(VIEWS.ACCOUNT);
const dynamicComponentRef = ref(null); // 用于引用动态组件实例

// 根据当前视图动态计算活动组件
let activeComponent = computed(() => {
  if (currentView.value === VIEWS.ACCOUNT)
    return AccountView;
  if (currentView.value === VIEWS.BACKTEST)
    return BackTestView;
  if (currentView.value === VIEWS.DESIGN_STATEGY)
    return StrategyFactory;
  if (currentView.value === VIEWS.DATA_CENTER)
    return DataCenter;
  if (currentView.value === VIEWS.SETTING_VIEW)
    return SettingView;
  return AccountView;
});

let activeIndex = ref(0);
let runningMode = ref('登录')
let showLogin = ref(false)
let cpu = ref(0)
// 1-展示已添加的服务器 2-展示已添加的交易所 3-新添加一个服务器 4-新添加一个交易所
let useOperaion = ref(0)

// 根据当前视图计算按钮状态
let is_account = computed(() => currentView.value === VIEWS.ACCOUNT);
let is_backtest = computed(() => currentView.value === VIEWS.BACKTEST);
let is_strategy = computed(() => currentView.value === VIEWS.DESIGN_STATEGY);
let is_datacenter = computed(() => currentView.value === VIEWS.DATA_CENTER);
let is_setting = computed(() => currentView.value === VIEWS.SETTING_VIEW);

onMounted(() => {
  console.log('mounted');
  activeComponent.value = AccountView
});
const emits = defineEmits(["refush", "onSettingChanged"]);

const onHandleBacktest = () => {
  console.info("onHandleBacktest");
  currentView.value = VIEWS.BACKTEST;
};

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
