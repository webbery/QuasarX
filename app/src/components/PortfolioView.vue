<template>
<div class="portfolio-view">
  <!-- 顶部工具栏 -->
  <div class="toolbar">
    <div class="toolbar-left">
      <h2><i class="fas fa-chart-pie"></i> 投资组合配置</h2>
      <span class="strategy-binding" v-if="currentStrategyId">
        <i class="fas fa-link"></i>
        关联策略：{{ strategyName || '未命名' }}
      </span>
    </div>
    <div class="toolbar-right">
      <select class="form-control" v-model="selectedConfigId" @change="loadConfig">
        <option value="">选择组合配置...</option>
        <option v-for="cfg in portfolioConfigs" :key="cfg.id" :value="cfg.id">
          {{ cfg.name }}
        </option>
      </select>
      <button class="btn btn-primary" @click="openNewConfigDialog">
        <i class="fas fa-plus"></i> 新建配置
      </button>
      <button class="btn" @click="saveConfig" :disabled="!hasChanges">
        <i class="fas fa-save"></i> 保存
      </button>
    </div>
  </div>

  <!-- 主内容区：两栏布局 -->
  <div class="main-content">
    <!-- 左栏：配置区 -->
    <div class="config-panel">

      <!-- 模型选择 -->
      <div class="card">
        <div class="card-header">
          <h3><i class="fas fa-brain"></i> 优化模型</h3>
        </div>
        <div class="card-content">
          <div class="form-group">
            <label>模型类型</label>
            <select class="form-control" v-model="config.modelType">
              <option value="black_litterman">Black-Litterman</option>
              <option value="mean_variance">Mean-Variance (马科维茨)</option>
              <option value="risk_parity">Risk Parity (风险平价)</option>
            </select>
          </div>
        </div>
      </div>

      <!-- 市场先验配置（仅 BL 模型显示） -->
      <div class="card" v-if="config.modelType === 'black_litterman'">
        <div class="card-header">
          <h3><i class="fas fa-chart-line"></i> 市场先验</h3>
        </div>
        <div class="card-content">
          <div class="form-row">
            <div class="form-group">
              <label>基准指数</label>
              <select class="form-control" v-model="config.params.benchmark">
                <option value="000300.SH">沪深 300</option>
                <option value="000905.SH">中证 500</option>
                <option value="399006.SZ">创业板指</option>
              </select>
            </div>
            <div class="form-group">
              <label>无风险利率 (%)</label>
              <input type="number" class="form-control" v-model.number="config.params.riskFreeRate" step="0.1" />
            </div>
          </div>
          <div class="form-row">
            <div class="form-group">
              <label>风险厌恶系数</label>
              <input type="number" class="form-control" v-model.number="config.params.riskAversion" step="0.1" />
            </div>
            <div class="form-group">
              <label>市场波动率 (%)</label>
              <input type="number" class="form-control" v-model.number="config.params.marketVolatility" step="0.1" />
            </div>
          </div>
        </div>
      </div>

      <!-- 证券池选择 -->
      <div class="card">
        <div class="card-header">
          <h3><i class="fas fa-list"></i> 证券池</h3>
          <button class="btn btn-sm" @click="selectSecurities">
            <i class="fas fa-edit"></i> 选择
          </button>
        </div>
        <div class="card-content">
          <div class="security-tags">
            <span v-for="sec in selectedSecurities" :key="sec.code" class="tag">
              {{ sec.code }} {{ sec.name }}
              <i class="fas fa-times" @click="removeSecurity(sec.code)"></i>
            </span>
            <span v-if="selectedSecurities.length === 0" class="empty-tip">
              暂无证券，请点击"选择"添加
            </span>
          </div>
        </div>
      </div>

      <!-- 投资者观点（仅 BL 模型显示） -->
      <div class="card" v-if="config.modelType === 'black_litterman'">
        <div class="card-header">
          <h3><i class="fas fa-lightbulb"></i> 投资者观点</h3>
          <button class="btn btn-sm btn-primary" @click="addView">
            <i class="fas fa-plus"></i> 添加观点
          </button>
        </div>
        <div class="card-content">
          <div class="views-list">
            <div v-for="(view, index) in config.views" :key="view.id" class="view-item">
              <div class="view-item-header">
                <span class="view-index">{{ index + 1 }}</span>
                <select class="form-control form-control-sm" v-model="view.type">
                  <option value="absolute">绝对观点</option>
                  <option value="relative">相对观点</option>
                </select>
                <button class="btn-icon" @click="removeView(index)">
                  <i class="fas fa-times"></i>
                </button>
              </div>

              <!-- 绝对观点 -->
              <div v-if="view.type === 'absolute'" class="view-content">
                <div class="form-group">
                  <label>证券</label>
                  <select class="form-control" v-model="view.security">
                    <option v-for="sec in selectedSecurities" :key="sec.code" :value="sec.code">
                      {{ sec.code }} {{ sec.name }}
                    </option>
                  </select>
                </div>
                <div class="form-group">
                  <label>预期收益率 (%)</label>
                  <input type="number" class="form-control" v-model.number="view.expectedReturn" step="0.1" />
                </div>
                <div class="form-group">
                  <label>置信度 ({{ view.confidence }}%)</label>
                  <input type="range" class="form-control range" v-model.number="view.confidence" min="0" max="100" />
                </div>
              </div>

              <!-- 相对观点 -->
              <div v-else class="view-content">
                <div class="form-group">
                  <label>做多证券</label>
                  <select class="form-control" v-model="view.longSecurity">
                    <option v-for="sec in selectedSecurities" :key="sec.code" :value="sec.code">
                      {{ sec.code }} {{ sec.name }}
                    </option>
                  </select>
                </div>
                <div class="form-group">
                  <label>做空证券</label>
                  <select class="form-control" v-model="view.shortSecurity">
                    <option v-for="sec in selectedSecurities" :key="sec.code" :value="sec.code">
                      {{ sec.code }} {{ sec.name }}
                    </option>
                  </select>
                </div>
                <div class="form-group">
                  <label>预期超额收益 (%)</label>
                  <input type="number" class="form-control" v-model.number="view.expectedExcessReturn" step="0.1" />
                </div>
                <div class="form-group">
                  <label>置信度 ({{ view.confidence }}%)</label>
                  <input type="range" class="form-control range" v-model.number="view.confidence" min="0" max="100" />
                </div>
              </div>
            </div>
            <div v-if="config.views.length === 0" class="empty-state">
              暂无观点，请点击上方"添加观点"按钮
            </div>
          </div>
        </div>
      </div>

      <!-- 约束条件 -->
      <div class="card">
        <div class="card-header">
          <h3><i class="fas fa-sliders-h"></i> 优化约束</h3>
        </div>
        <div class="card-content">
          <div class="form-row">
            <div class="form-group">
              <label>单证券最大权重 (%)</label>
              <input type="number" class="form-control" v-model.number="constraints.maxWeight" min="0" max="100" />
            </div>
            <div class="form-group">
              <label>单证券最小权重 (%)</label>
              <input type="number" class="form-control" v-model.number="constraints.minWeight" min="0" max="100" />
            </div>
          </div>
          <div class="form-row checkbox-row">
            <label class="checkbox-label">
              <input type="checkbox" v-model="constraints.longOnly" />
              <span>不允许做空（权重 ≥ 0）</span>
            </label>
            <label class="checkbox-label">
              <input type="checkbox" v-model="constraints.fullyInvested" />
              <span>完全投资（权重之和 = 1）</span>
            </label>
          </div>
        </div>
      </div>

      <!-- 执行优化按钮 -->
      <div class="action-buttons">
        <button class="btn btn-primary btn-lg btn-block" @click="runOptimization" :disabled="!canOptimize">
          <i class="fas fa-calculator"></i> 执行优化计算
        </button>
      </div>

    </div>

    <!-- 右栏：结果展示 -->
    <div class="result-panel">

      <!-- 核心指标卡片 -->
      <div class="card">
        <div class="card-header">
          <h3><i class="fas fa-tachometer-alt"></i> 优化结果</h3>
        </div>
        <div class="card-content">
          <div class="metrics-grid">
            <div class="metric-card">
              <div class="metric-label">预期年化收益</div>
              <div class="metric-value" :class="result.expectedReturn > 0 ? 'positive' : 'negative'">
                {{ hasResult ? result.expectedReturn.toFixed(2) : '--' }}%
              </div>
            </div>
            <div class="metric-card">
              <div class="metric-label">预期波动率</div>
              <div class="metric-value">
                {{ hasResult ? result.volatility.toFixed(2) : '--' }}%
              </div>
            </div>
            <div class="metric-card">
              <div class="metric-label">夏普比率</div>
              <div class="metric-value">
                {{ hasResult ? result.sharpeRatio.toFixed(2) : '--' }}
              </div>
            </div>
            <div class="metric-card">
              <div class="metric-label">最大回撤</div>
              <div class="metric-value negative">
                {{ hasResult ? result.maxDrawdown.toFixed(2) : '--' }}%
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- 权重分配图表 -->
      <div class="card">
        <div class="card-header">
          <h3><i class="fas fa-chart-pie"></i> 权重分配</h3>
        </div>
        <div class="card-content">
          <div class="chart-container" id="weightChart"></div>
        </div>
      </div>

      <!-- 权重明细表格 -->
      <div class="card">
        <div class="card-header">
          <h3><i class="fas fa-table"></i> 权重明细</h3>
        </div>
        <div class="card-content table-content">
          <table class="data-table">
            <thead>
              <tr>
                <th>代码</th>
                <th>名称</th>
                <th>权重 (%)</th>
                <th>变化</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="item in weightTableData" :key="item.code">
                <td>{{ item.code }}</td>
                <td>{{ item.name }}</td>
                <td :class="item.weight > 0 ? 'positive' : 'negative'">
                  {{ item.weight.toFixed(2) }}
                </td>
                <td :class="item.change > 0 ? 'positive' : item.change < 0 ? 'negative' : ''">
                  {{ item.change > 0 ? '+' : '' }}{{ item.change.toFixed(2) }}
                </td>
              </tr>
              <tr v-if="weightTableData.length === 0">
                <td colspan="4" class="empty-cell">暂无优化结果</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

    </div>
  </div>

  <!-- 证券选择对话框 -->
  <div class="modal" v-if="showSecurityDialog" @click.self="closeSecurityDialog">
    <div class="modal-content modal-lg">
      <div class="modal-header">
        <h3>选择证券池</h3>
        <span class="close" @click="closeSecurityDialog">&times;</span>
      </div>
      <div class="modal-body">
        <div class="security-selection">
          <div class="selection-left">
            <label>可选证券</label>
            <select class="form-control multi-select" multiple v-model="tempSelectedSecurities">
              <option v-for="sec in availableSecurities" :key="sec.code" :value="sec.code">
                {{ sec.code }} - {{ sec.name }}
              </option>
            </select>
          </div>
          <div class="selection-actions">
            <button class="btn" @click="selectAll">
              <i class="fas fa-angle-double-right"></i>
            </button>
            <button class="btn" @click="clearAll">
              <i class="fas fa-angle-double-left"></i>
            </button>
          </div>
          <div class="selection-right">
            <label>已选证券</label>
            <select class="form-control multi-select" multiple v-model="tempSelectedSecurities">
              <option v-for="sec in selectedSecurities" :key="sec.code" :value="sec.code">
                {{ sec.code }} - {{ sec.name }}
              </option>
            </select>
          </div>
        </div>
      </div>
      <div class="modal-footer">
        <button class="btn" @click="closeSecurityDialog">取消</button>
        <button class="btn btn-primary" @click="confirmSecurities">确定</button>
      </div>
    </div>
  </div>

  <!-- 保存配置对话框 -->
  <div class="modal" v-if="showSaveDialog" @click.self="closeSaveDialog">
    <div class="modal-content modal-sm">
      <div class="modal-header">
        <h3>保存配置</h3>
        <span class="close" @click="closeSaveDialog">&times;</span>
      </div>
      <div class="modal-body">
        <div class="form-group">
          <label>配置名称</label>
          <input type="text" class="form-control" v-model="configName" placeholder="输入配置名称" />
        </div>
      </div>
      <div class="modal-footer">
        <button class="btn" @click="closeSaveDialog">取消</button>
        <button class="btn btn-primary" @click="confirmSave">保存</button>
      </div>
    </div>
  </div>

</div>
</template>

<script setup>
import { ref, computed, onMounted, inject, watch } from 'vue';
import axios from 'axios';

// 从父组件注入的数据
const currentStrategyId = inject('currentStrategyId', ref(''));
const portfolioConfigs = inject('portfolioConfigs', ref([]));
const currentPortfolioConfig = inject('currentPortfolioConfig', ref(null));

// 状态变量
const selectedConfigId = ref('');
const strategyName = ref('');
const hasChanges = ref(false);
const showSecurityDialog = ref(false);
const showSaveDialog = ref(false);
const configName = ref('');
const tempSelectedSecurities = ref([]);

// 默认可选证券（可从策略图中获取）
const availableSecurities = ref([
  { code: '000001', name: '平安银行' },
  { code: '000002', name: '万科 A' },
  { code: '600036', name: '招商银行' },
  { code: '600519', name: '贵州茅台' },
  { code: '000858', name: '五粮液' },
  { code: '601318', name: '中国平安' },
  { code: '600030', name: '中信证券' },
  { code: '300750', name: '宁德时代' }
]);

// 选中的证券
const selectedSecurities = ref([]);

// 配置对象
const config = ref({
  modelType: 'black_litterman',
  params: {
    benchmark: '000300.SH',
    riskFreeRate: 2.5,
    riskAversion: 2.5,
    marketVolatility: 15.0
  },
  views: []
});

// 约束条件
const constraints = ref({
  maxWeight: 20,
  minWeight: 0,
  longOnly: true,
  fullyInvested: true
});

// 优化结果
const result = ref({
  expectedReturn: 0,
  volatility: 0,
  sharpeRatio: 0,
  maxDrawdown: 0
});

const hasResult = ref(false);

// 权重表格数据
const weightTableData = computed(() => {
  if (!result.value.weights) return [];
  return result.value.weights.map(w => ({
    code: w.code,
    name: w.name || '',
    weight: w.weight * 100,
    change: (w.weight - 1 / selectedSecurities.value.length) * 100
  }));
});

// 是否可以优化
const canOptimize = computed(() => {
  return selectedSecurities.value.length > 0;
});

// 监听策略 ID 变化
watch(currentStrategyId, (newId) => {
  if (newId) {
    loadPortfolioConfigs(newId);
  }
});

// 加载组合配置列表
const loadPortfolioConfigs = async (strategyId) => {
  try {
    // TODO: 从 API 或本地存储加载
    const stored = localStorage.getItem(`portfolio_configs_${strategyId}`);
    if (stored) {
      portfolioConfigs.value = JSON.parse(stored);
    }
  } catch (error) {
    console.error('加载组合配置失败:', error);
  }
};

// 加载选中的配置
const loadConfig = async () => {
  if (!selectedConfigId.value) {
    resetConfig();
    return;
  }

  const cfg = portfolioConfigs.value.find(c => c.id === selectedConfigId.value);
  if (cfg) {
    config.value = JSON.parse(JSON.stringify(cfg));
    selectedSecurities.value = cfg.securities || [];
    constraints.value = cfg.constraints || constraints.value;
    hasChanges.value = false;
  }
};

// 重置配置
const resetConfig = () => {
  config.value = {
    modelType: 'black_litterman',
    params: {
      benchmark: '000300.SH',
      riskFreeRate: 2.5,
      riskAversion: 2.5,
      marketVolatility: 15.0
    },
    views: []
  };
  selectedSecurities.value = [];
  constraints.value = {
    maxWeight: 20,
    minWeight: 0,
    longOnly: true,
    fullyInvested: true
  };
  hasResult.value = false;
  hasChanges.value = false;
};

// 打开新建配置对话框
const openNewConfigDialog = () => {
  configName.value = '';
  showSaveDialog.value = true;
};

// 保存配置
const saveConfig = () => {
  if (selectedConfigId.value) {
    // 更新现有配置
    updateConfig();
  } else {
    // 新建配置
    showSaveDialog.value = true;
  }
};

// 确认保存
const confirmSave = async () => {
  if (!configName.value.trim()) {
    alert('请输入配置名称');
    return;
  }

  const newConfig = {
    id: 'port_' + Date.now(),
    name: configName.value,
    strategyId: currentStrategyId.value,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    modelType: config.value.modelType,
    params: config.value.params,
    views: config.value.views,
    securities: [...selectedSecurities.value],
    constraints: { ...constraints.value },
    optimizationResult: hasResult.value ? { ...result.value } : null
  };

  portfolioConfigs.value.push(newConfig);
  selectedConfigId.value = newConfig.id;

  // 保存到本地存储
  await saveToStorage();

  showSaveDialog.value = false;
  hasChanges.value = false;
};

// 更新配置
const updateConfig = async () => {
  const cfg = portfolioConfigs.value.find(c => c.id === selectedConfigId.value);
  if (cfg) {
    cfg.updatedAt = Date.now();
    cfg.modelType = config.value.modelType;
    cfg.params = config.value.params;
    cfg.views = config.value.views;
    cfg.securities = [...selectedSecurities.value];
    cfg.constraints = { ...constraints.value };
    if (hasResult.value) {
      cfg.optimizationResult = { ...result.value };
    }
  }

  await saveToStorage();
  hasChanges.value = false;
};

// 保存到本地存储
const saveToStorage = async () => {
  if (currentStrategyId.value) {
    localStorage.setItem(
      `portfolio_configs_${currentStrategyId.value}`,
      JSON.stringify(portfolioConfigs.value)
    );
  }
};

// 选择证券
const selectSecurities = () => {
  tempSelectedSecurities.value = selectedSecurities.value.map(s => s.code);
  showSecurityDialog.value = true;
};

// 关闭证券选择对话框
const closeSecurityDialog = () => {
  showSecurityDialog.value = false;
};

// 确认证券选择
const confirmSecurities = () => {
  selectedSecurities.value = availableSecurities.value.filter(
    s => tempSelectedSecurities.value.includes(s.code)
  );
  showSecurityDialog.value = false;
  hasChanges.value = true;
};

// 全选
const selectAll = () => {
  tempSelectedSecurities.value = availableSecurities.value.map(s => s.code);
};

// 清空
const clearAll = () => {
  tempSelectedSecurities.value = [];
};

// 移除证券
const removeSecurity = (code) => {
  selectedSecurities.value = selectedSecurities.value.filter(s => s.code !== code);
  hasChanges.value = true;
};

// 添加观点
const addView = () => {
  config.value.views.push({
    id: 'view_' + Date.now(),
    type: 'absolute',
    security: '',
    expectedReturn: 0,
    confidence: 50,
    longSecurity: '',
    shortSecurity: '',
    expectedExcessReturn: 0
  });
  hasChanges.value = true;
};

// 移除观点
const removeView = (index) => {
  config.value.views.splice(index, 1);
  hasChanges.value = true;
};

// 执行优化
const runOptimization = async () => {
  if (!canOptimize.value) {
    alert('请先选择证券池');
    return;
  }

  try {
    // TODO: 调用后端 API 执行优化
    // 这里先模拟一个结果
    const mockResult = await mockOptimize();
    result.value = mockResult;
    hasResult.value = true;
    hasChanges.value = true;

    // 绘制图表
    setTimeout(() => {
      renderWeightChart();
    }, 100);
  } catch (error) {
    console.error('优化计算失败:', error);
    alert('优化计算失败，请重试');
  }
};

// 模拟优化结果
const mockOptimize = async () => {
  return new Promise(resolve => {
    setTimeout(() => {
      const count = selectedSecurities.value.length || 1;
      const weights = selectedSecurities.value.map((s, i) => ({
        code: s.code,
        name: s.name,
        weight: 0.1 + Math.random() * 0.2
      }));

      // 归一化权重
      const total = weights.reduce((sum, w) => sum + w.weight, 0);
      weights.forEach(w => w.weight /= total);

      resolve({
        expectedReturn: 8 + Math.random() * 4,
        volatility: 12 + Math.random() * 5,
        sharpeRatio: 0.5 + Math.random() * 0.5,
        maxDrawdown: 5 + Math.random() * 5,
        weights: weights
      });
    }, 500);
  });
};

// 渲染权重图表
const renderWeightChart = () => {
  // TODO: 使用 ECharts 渲染饼图
  console.log('render weight chart');
};

// 关闭保存对话框
const closeSaveDialog = () => {
  showSaveDialog.value = false;
};

onMounted(() => {
  if (currentStrategyId.value) {
    loadPortfolioConfigs(currentStrategyId.value);
  }
});
</script>

<style scoped lang="scss">
.portfolio-view {
  height: 100%;
  display: flex;
  flex-direction: column;
  background-color: var(--dark-bg, #1e1e1e);
  overflow: hidden;
}

.toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 15px 20px;
  border-bottom: 1px solid var(--border, #333);
  background-color: var(--panel-bg, #2a2a2a);

  h2 {
    margin: 0;
    font-size: 1.3rem;
    display: flex;
    align-items: center;
    gap: 10px;
    color: var(--text, rgba(255, 255, 255, 0.87));
  }

  .strategy-binding {
    margin-left: 15px;
    padding: 5px 12px;
    background-color: rgba(41, 98, 255, 0.1);
    border-radius: 4px;
    font-size: 0.85rem;
    color: var(--primary, #2962ff);
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .toolbar-right {
    display: flex;
    gap: 10px;
    align-items: center;
  }
}

.main-content {
  flex: 1;
  display: grid;
  grid-template-columns: 1fr 400px;
  gap: 10px;
  padding: 10px;
  overflow: hidden;
}

.config-panel, .result-panel {
  overflow-y: auto;
}

.card {
  background-color: var(--panel-bg, #2a2a2a);
  border-radius: 8px;
  border: 1px solid var(--border, #333);
  margin-bottom: 10px;

  .card-header {
    padding: 12px 15px;
    border-bottom: 1px solid var(--border, #333);
    display: flex;
    justify-content: space-between;
    align-items: center;

    h3 {
      margin: 0;
      font-size: 1rem;
      font-weight: 600;
      color: var(--text, rgba(255, 255, 255, 0.87));
    }
  }

  .card-content {
    padding: 15px;
  }
}

.form-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 15px;
  margin-bottom: 15px;

  &:last-child {
    margin-bottom: 0;
  }
}

.form-group {
  margin-bottom: 12px;

  label {
    display: block;
    margin-bottom: 6px;
    font-size: 0.85rem;
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  }

  .form-control {
    width: 100%;
    padding: 8px 12px;
    background-color: var(--darker-bg, #1e1e1e);
    border: 1px solid var(--border, #333);
    border-radius: 4px;
    color: var(--text, rgba(255, 255, 255, 0.87));
    font-size: 0.9rem;

    &:focus {
      outline: none;
      border-color: var(--primary, #2962ff);
    }

    &.range {
      padding: 0;
    }

    &.form-control-sm {
      padding: 4px 8px;
      font-size: 0.85rem;
    }
  }
}

.checkbox-row {
  display: flex;
  gap: 20px;

  .checkbox-label {
    display: flex;
    align-items: center;
    gap: 8px;
    cursor: pointer;

    input[type="checkbox"] {
      width: 16px;
      height: 16px;
      cursor: pointer;
    }

    span {
      font-size: 0.85rem;
      color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    }
  }
}

.security-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;

  .tag {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 4px 10px;
    background-color: rgba(41, 98, 255, 0.1);
    border-radius: 4px;
    font-size: 0.85rem;
    color: var(--text, rgba(255, 255, 255, 0.87));

    i {
      cursor: pointer;
      &:hover {
        color: var(--danger, #f44336);
      }
    }
  }

  .empty-tip {
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    font-size: 0.85rem;
  }
}

.view-item {
  border: 1px solid var(--border, #333);
  border-radius: 6px;
  padding: 12px;
  margin-bottom: 10px;

  .view-item-header {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 12px;

    .view-index {
      width: 24px;
      height: 24px;
      border-radius: 50%;
      background-color: var(--primary, #2962ff);
      color: white;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 0.75rem;
      font-weight: 600;
    }

    .form-control-sm {
      flex: 1;
    }
  }

  .view-content {
    .form-group {
      margin-bottom: 10px;

      &:last-child {
        margin-bottom: 0;
      }
    }
  }
}

.empty-state {
  text-align: center;
  padding: 20px;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  font-size: 0.85rem;
}

.btn {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border: none;
  padding: 8px 16px;
  border-radius: 4px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  display: inline-flex;
  align-items: center;
  gap: 6px;

  &:hover:not(:disabled) {
    background: linear-gradient(90deg, #1d4ed8, #1e40af);
    transform: translateY(-1px);
  }

  &:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  &.btn-primary {
    background: linear-gradient(90deg, #2962ff, #1e40af);
  }

  &.btn-sm {
    padding: 4px 10px;
    font-size: 0.85rem;
  }

  &.btn-lg {
    padding: 12px 20px;
    font-size: 1rem;
  }

  &.btn-block {
    width: 100%;
    justify-content: center;
  }
}

.btn-icon {
  background: none;
  border: none;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  cursor: pointer;
  padding: 4px;
  display: inline-flex;
  align-items: center;
  justify-content: center;

  &:hover {
    color: var(--danger, #f44336);
  }
}

.action-buttons {
  margin-top: 20px;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;

  .metric-card {
    padding: 15px;
    background-color: var(--darker-bg, #1e1e1e);
    border-radius: 6px;
    text-align: center;

    .metric-label {
      font-size: 0.75rem;
      color: var(--text-secondary, rgba(255, 255, 255, 0.6));
      margin-bottom: 8px;
    }

    .metric-value {
      font-size: 1.25rem;
      font-weight: 600;
      color: var(--text, rgba(255, 255, 255, 0.87));

      &.positive {
        color: var(--secondary, #00c853);
      }

      &.negative {
        color: var(--danger, #f44336);
      }
    }
  }
}

.data-table {
  width: 100%;
  border-collapse: collapse;

  th, td {
    padding: 10px;
    text-align: left;
    border-bottom: 1px solid var(--border, #333);
  }

  th {
    font-weight: 500;
    font-size: 0.85rem;
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  }

  td {
    color: var(--text, rgba(255, 255, 255, 0.87));
    font-size: 0.9rem;

    &.positive {
      color: var(--secondary, #00c853);
    }

    &.negative {
      color: var(--danger, #f44336);
    }
  }

  .empty-cell {
    text-align: center;
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    padding: 30px !important;
  }
}

.table-content {
  padding: 0 !important;

  .data-table {
    margin-bottom: 0;
  }
}

.chart-container {
  height: 200px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  font-size: 0.85rem;
}

.modal {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.6);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 1000;

  .modal-content {
    background-color: var(--panel-bg, #2a2a2a);
    border-radius: 8px;
    width: 450px;
    max-width: 90%;
    border: 1px solid var(--border, #333);

    &.modal-lg {
      width: 700px;
    }

    &.modal-sm {
      width: 350px;
    }

    .modal-header {
      padding: 15px 20px;
      border-bottom: 1px solid var(--border, #333);
      display: flex;
      justify-content: space-between;
      align-items: center;

      h3 {
        margin: 0;
        font-size: 1.1rem;
        color: var(--text, rgba(255, 255, 255, 0.87));
      }

      .close {
        font-size: 1.5rem;
        cursor: pointer;
        color: var(--text-secondary, rgba(255, 255, 255, 0.6));

        &:hover {
          color: var(--text, rgba(255, 255, 255, 0.87));
        }
      }
    }

    .modal-body {
      padding: 20px;
    }

    .modal-footer {
      padding: 15px 20px;
      border-top: 1px solid var(--border, #333);
      display: flex;
      justify-content: flex-end;
      gap: 10px;
    }
  }
}

.security-selection {
  display: grid;
  grid-template-columns: 1fr auto 1fr;
  gap: 15px;
  align-items: center;

  label {
    display: block;
    margin-bottom: 6px;
    font-size: 0.85rem;
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  }

  .multi-select {
    height: 200px;
    resize: none;
  }

  .selection-actions {
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding-top: 20px;
  }
}
</style>
