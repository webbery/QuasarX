<script setup >
import { defineOptions, ref, defineEmits } from "vue";
import LabVue from "./components/Lab.vue";
import MineVue from "./components/Mine.vue";
import RiskManagerVue from "./components/RiskManager.vue";
import StrategyVue from "./components/Strategy.vue";
import MarketPanel from "./components/MarketPanel.vue";
import AccountView from "./components/AccountView.vue";

const currentTab = ref("mine");

const tabs = {
  mine: MineVue,
  risk: RiskManagerVue,
  strategy: StrategyVue,
};

const tabName = {
  mine: "我的",
  risk: "风险管理",
  strategy: "策略管理",
};
const emits = defineEmits(["refush"]);

const onHandleRisk = () => {
  console.info("handleRisk 2");
  currentComponent = ref("RiskManagerVue");
};

const onHandleMine = () => {
  console.info("handle Mine");
  currentComponent = ref("MineVue");
};
</script>

<template>
    <!-- 头部 -->
    <header>
      <div class="logo">
        <i class="fas fa-chart-network"></i>
        <span>QuasarX</span>
      </div>
      <div class="header-controls">
        <button class="control-btn">
          <i class="fas fa-sync-alt"></i> 刷新数据
        </button>
        <button class="control-btn">
          <i class="fas fa-cloud-upload-alt"></i> 部署策略
        </button>
        <button class="control-btn"><i class="fas fa-cog"></i> 设置</button>
      </div>
    </header>

    <!-- 左侧导航 -->
    <nav class="sidebar">
      <div class="nav-section">
        <div class="nav-title">交易</div>
        <div class="nav-item active">
          <i class="fas fa-home"></i>
          <span>账户总览</span>
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
        <div class="nav-item">
          <i class="fas fa-industry"></i>
          <span>策略工厂</span>
        </div>
        <div class="nav-item">
          <i class="fas fa-project-diagram"></i>
          <span>策略回测</span>
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
        <div class="nav-item">
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
      <AccountView></AccountView>
    </main>

    <!-- 右侧面板 -->
    <aside class="right-panel">
      <MarketPanel></MarketPanel>

      <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-radiation"></i> 风险指标</h3>
          <span>当前持仓</span>
        </div>

        <div class="stats-grid">
          <div class="stat-item">
            <div class="stat-title">Delta 暴露</div>
            <div class="stat-value">0.42</div>
            <div>股票多头</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">Gamma 暴露</div>
            <div class="stat-value">0.08</div>
            <div>期权持仓</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">Vega 暴露</div>
            <div class="stat-value">-0.15</div>
            <div>波动率空头</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">Beta 加权</div>
            <div class="stat-value">1.12</div>
            <div>市场敏感度</div>
          </div>
        </div>
      </div>

      <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-exclamation-triangle"></i> 压力测试结果</h3>
          <span>最新</span>
        </div>

        <div class="stats-grid">
          <div class="stat-item">
            <div class="stat-title">最大回撤</div>
            <div class="stat-value negative">-15.8%</div>
            <div>黑天鹅场景</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">净值下跌</div>
            <div class="stat-value negative">-12.3%</div>
            <div>流动性危机</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">恢复概率</div>
            <div class="stat-value">78%</div>
            <div>6个月内</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">压力指数</div>
            <div class="stat-value negative">7.2/10</div>
            <div>高风险</div>
          </div>
        </div>
      </div>
    </aside>

    <!-- 页脚 -->
    <footer>
      <div>QuasarX v0.0.1 | 实盘交易模式</div>
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
          <span>CPU: 42%</span>
        </div>
        <div class="status-item">
          <i class="fas fa-memory"></i>
          <span>内存: 3.2G/8G</span>
        </div>
      </div>
    </footer>
</template>

<style scoped>
    
</style>
