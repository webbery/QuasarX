<template>
<div class="portfolio-panel">
  <!-- 当前组合信息 -->
  <div class="panel-section">
    <div class="panel-title">
      <h3><i class="fas fa-layer-group"></i> 当前配置</h3>
    </div>
    <div class="portfolio-summary">
      <div class="summary-item">
        <span class="label">配置名称</span>
        <span class="value">{{ currentConfig?.name || '未选择' }}</span>
      </div>
      <div class="summary-item">
        <span class="label">模型类型</span>
        <span class="value">{{ modelTypeText }}</span>
      </div>
      <div class="summary-item">
        <span class="label">证券数量</span>
        <span class="value">{{ currentConfig?.securities?.length || 0 }}</span>
      </div>
      <div class="summary-item">
        <span class="label">观点数量</span>
        <span class="value">{{ currentConfig?.views?.length || 0 }}</span>
      </div>
    </div>
  </div>

  <!-- 观点摘要 -->
  <div class="panel-section">
    <div class="panel-title">
      <h3><i class="fas fa-lightbulb"></i> 观点摘要</h3>
    </div>
    <div class="views-summary">
      <div v-for="(view, i) in currentConfig?.views" :key="i" class="view-summary-item">
        <span class="view-type-badge" :class="view.type === 'absolute' ? 'type-absolute' : 'type-relative'">
          {{ view.type === 'absolute' ? '绝对' : '相对' }}
        </span>
        <span class="view-desc">
          <template v-if="view.type === 'absolute'">
            {{ getSecurityName(view.security) }} → {{ view.expectedReturn }}%
          </template>
          <template v-else>
            {{ getSecurityName(view.longSecurity) }} vs {{ getSecurityName(view.shortSecurity) }}
          </template>
        </span>
        <span class="view-confidence">{{ view.confidence }}%</span>
      </div>
      <div v-if="!currentConfig?.views?.length" class="empty-tip">暂无观点</div>
    </div>
  </div>

  <!-- 约束摘要 -->
  <div class="panel-section">
    <div class="panel-title">
      <h3><i class="fas fa-clipboard-list"></i> 约束条件</h3>
    </div>
    <div class="constraints-summary">
      <div class="constraint-item">
        <span>权重范围</span>
        <span>{{ currentConfig?.constraints?.minWeight || 0 }}% - {{ currentConfig?.constraints?.maxWeight || 100 }}%</span>
      </div>
      <div class="constraint-item">
        <span>做空</span>
        <span :class="currentConfig?.constraints?.longOnly ? 'disabled' : 'enabled'">
          {{ currentConfig?.constraints?.longOnly !== false ? '不允许' : '允许' }}
        </span>
      </div>
      <div class="constraint-item">
        <span>完全投资</span>
        <span :class="currentConfig?.constraints?.fullyInvested ? 'enabled' : 'disabled'">
          {{ currentConfig?.constraints?.fullyInvested !== false ? '是' : '否' }}
        </span>
      </div>
    </div>
  </div>

  <!-- 优化结果摘要 -->
  <div class="panel-section" v-if="currentConfig?.optimizationResult">
    <div class="panel-title">
      <h3><i class="fas fa-chart-line"></i> 优化结果</h3>
    </div>
    <div class="result-summary">
      <div class="result-item">
        <span class="result-label">预期收益</span>
        <span class="result-value positive">{{ currentConfig.optimizationResult.expectedReturn?.toFixed(2) }}%</span>
      </div>
      <div class="result-item">
        <span class="result-label">波动率</span>
        <span class="result-value">{{ currentConfig.optimizationResult.volatility?.toFixed(2) }}%</span>
      </div>
      <div class="result-item">
        <span class="result-label">夏普比率</span>
        <span class="result-value">{{ currentConfig.optimizationResult.sharpeRatio?.toFixed(2) }}</span>
      </div>
      <div class="result-item">
        <span class="result-label">最大回撤</span>
        <span class="result-value negative">{{ currentConfig.optimizationResult.maxDrawdown?.toFixed(2) }}%</span>
      </div>
    </div>
  </div>

  <!-- 快速操作 -->
  <div class="panel-section">
    <div class="panel-title">
      <h3><i class="fas fa-bolt"></i> 快捷操作</h3>
    </div>
    <div class="quick-actions">
      <button class="btn btn-block" @click="$emit('optimize')">
        <i class="fas fa-calculator"></i> 重新优化
      </button>
      <button class="btn btn-block" @click="$emit('export')">
        <i class="fas fa-download"></i> 导出配置
      </button>
    </div>
  </div>
</div>
</template>

<script setup>
import { computed, inject, ref } from 'vue';

// 注入当前组合配置
const currentPortfolioConfig = inject('currentPortfolioConfig', ref(null));

// 使用注入的配置
const currentConfig = computed(() => currentPortfolioConfig?.value);

// 定义 emit
const emit = defineEmits(['optimize', 'export']);

// 获取证券名称（从可用证券列表中查找）
const availableSecurities = [
  { code: '000001', name: '平安银行' },
  { code: '000002', name: '万科 A' },
  { code: '600036', name: '招商银行' },
  { code: '600519', name: '贵州茅台' },
  { code: '000858', name: '五粮液' },
  { code: '601318', name: '中国平安' },
  { code: '600030', name: '中信证券' },
  { code: '300750', name: '宁德时代' }
];

const getSecurityName = (code) => {
  if (!code) return '';
  const sec = availableSecurities.find(s => s.code === code);
  return sec ? sec.name : code;
};

// 模型类型文本
const modelTypeText = computed(() => {
  const map = {
    'black_litterman': 'Black-Litterman',
    'mean_variance': 'Mean-Variance',
    'risk_parity': 'Risk Parity'
  };
  return map[currentConfig?.value?.modelType] || '-';
});
</script>

<style scoped lang="scss">
.portfolio-panel {
  height: 100%;
  overflow-y: auto;
  background-color: var(--panel-bg, #2a2a2a);
  padding: 10px;
}

.panel-section {
  background-color: var(--panel-bg, #2a2a2a);
  border-radius: 8px;
  border: 1px solid var(--border, #333);
  margin-bottom: 10px;
  padding: 15px;

  .panel-title {
    margin-bottom: 15px;

    h3 {
      margin: 0;
      font-size: 1rem;
      font-weight: 600;
      color: var(--text, rgba(255, 255, 255, 0.87));
      display: flex;
      align-items: center;
      gap: 8px;
    }
  }
}

.portfolio-summary {
  .summary-item {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid var(--border, #333);

    &:last-child {
      border-bottom: none;
    }

    .label {
      font-size: 0.85rem;
      color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    }

    .value {
      font-size: 0.85rem;
      color: var(--text, rgba(255, 255, 255, 0.87));
      font-weight: 500;
    }
  }
}

.views-summary {
  .view-summary-item {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 0;
    border-bottom: 1px solid var(--border, #333);

    &:last-child {
      border-bottom: none;
    }

    .view-type-badge {
      padding: 2px 8px;
      border-radius: 4px;
      font-size: 0.75rem;
      font-weight: 500;

      &.type-absolute {
        background-color: rgba(41, 98, 255, 0.2);
        color: var(--primary, #2962ff);
      }

      &.type-relative {
        background-color: rgba(156, 39, 176, 0.2);
        color: #9c27b0;
      }
    }

    .view-desc {
      flex: 1;
      font-size: 0.85rem;
      color: var(--text, rgba(255, 255, 255, 0.87));
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }

    .view-confidence {
      font-size: 0.75rem;
      color: var(--text-secondary, rgba(255, 255, 255, 0.6));
      background-color: var(--darker-bg, #1e1e1e);
      padding: 2px 6px;
      border-radius: 4px;
    }
  }

  .empty-tip {
    text-align: center;
    padding: 15px;
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    font-size: 0.85rem;
  }
}

.constraints-summary {
  .constraint-item {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid var(--border, #333);

    &:last-child {
      border-bottom: none;
    }

    span {
      font-size: 0.85rem;
      color: var(--text, rgba(255, 255, 255, 0.87));
    }

    span:last-child {
      &.enabled {
        color: var(--secondary, #00c853);
        font-weight: 500;
      }

      &.disabled {
        color: var(--text-secondary, rgba(255, 255, 255, 0.6));
      }
    }
  }
}

.result-summary {
  .result-item {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid var(--border, #333);

    &:last-child {
      border-bottom: none;
    }

    .result-label {
      font-size: 0.85rem;
      color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    }

    .result-value {
      font-size: 0.9rem;
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

.quick-actions {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.btn {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border: none;
  padding: 10px 16px;
  border-radius: 6px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  font-size: 0.9rem;

  &:hover:not(:disabled) {
    background: linear-gradient(90deg, #1d4ed8, #1e40af);
    transform: translateY(-1px);
  }

  &.btn-block {
    width: 100%;
  }
}
</style>
