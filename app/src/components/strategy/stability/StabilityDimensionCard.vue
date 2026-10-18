<template>
  <div class="dimension-card">
    <div class="card-header">
      <h4>
        <i :class="dimension.icon"></i>
        {{ dimension.label }}
      </h4>
      <span class="source-badge" :class="dimension.source">
        {{ SOURCE_LABEL[dimension.source] }}
      </span>
    </div>

    <p class="card-summary">{{ dimension.summary }}</p>

    <!-- 子检验（维度细分时） -->
    <template v-if="dimension.subChecks">
      <div v-for="sub in dimension.subChecks" :key="sub.id" class="sub-check">
        <div class="sub-header">
          <span class="sub-label">{{ sub.label }}</span>
          <span class="impl-badge" :class="{ on: sub.implemented }">
            {{ sub.implemented ? '已接入' : '待接入' }}
          </span>
        </div>

        <p v-if="sub.pendingNote" class="pending-note">{{ sub.pendingNote }}</p>

        <div class="section">
          <div class="section-title">扰动因子</div>
          <ul class="factor-list">
            <li v-for="factor in sub.factors" :key="factor.key">
              <span class="factor-label">{{ factor.label }}</span>
              <span class="factor-value">{{ factor.defaultValue }}</span>
              <span v-if="factor.note" class="factor-note">{{ factor.note }}</span>
            </li>
          </ul>
        </div>

        <div class="section">
          <div class="section-title">每次运行采集</div>
          <div class="metric-chips">
            <span v-for="metric in sub.metrics" :key="metric" class="metric-chip">
              {{ metric }}
            </span>
          </div>
        </div>

        <div class="section">
          <div class="section-title">判定标准</div>
          <p class="verdict-text">{{ sub.verdict }}</p>
        </div>
      </div>
    </template>

    <!-- 无子检验时直接列因子 -->
    <template v-else>
      <div class="section">
        <div class="section-title">扰动因子</div>
        <ul class="factor-list">
          <li v-for="factor in dimension.factors" :key="factor.key">
            <span class="factor-label">{{ factor.label }}</span>
            <span class="factor-value">{{ factor.defaultValue }}</span>
            <span v-if="factor.note" class="factor-note">{{ factor.note }}</span>
          </li>
        </ul>
      </div>

      <div class="section">
        <div class="section-title">每次运行采集</div>
        <div class="metric-chips">
          <span v-for="metric in dimension.metrics" :key="metric" class="metric-chip">
            {{ metric }}
          </span>
        </div>
      </div>

      <div class="section">
        <div class="section-title">判定标准</div>
        <p class="verdict-text">{{ dimension.verdict }}</p>
      </div>
    </template>

    <!-- 后端依赖 -->
    <div v-if="dimension.backendNeed" class="backend-need">
      <i class="fas fa-circle-info"></i>
      <span>{{ dimension.backendNeed }}</span>
    </div>

    <!-- 结果占位 -->
    <div class="result-placeholder">
      <i class="fas fa-chart-area"></i>
      <span>敏感性曲线与红/黄/绿判定待接入</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { StabilityDimension } from './stabilityDimensions'
import { SOURCE_LABEL } from './stabilityDimensions'

defineProps<{ dimension: StabilityDimension }>()
</script>

<style scoped lang="scss">
.dimension-card {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 16px 18px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;

  h4 {
    margin: 0;
    font-size: 15px;
    font-weight: 600;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 8px;

    i {
      color: var(--primary);
    }
  }
}

.source-badge {
  flex-shrink: 0;
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 10px;
  border: 1px solid var(--border);
  color: var(--text-secondary);
  white-space: nowrap;

  &.backend {
    border-color: color-mix(in srgb, var(--accent) 45%, transparent);
    color: var(--accent);
    background: color-mix(in srgb, var(--accent) 10%, transparent);
  }

  &.frontend {
    border-color: color-mix(in srgb, var(--secondary) 45%, transparent);
    color: var(--secondary);
    background: color-mix(in srgb, var(--secondary) 10%, transparent);
  }
}

.card-summary {
  margin: 0;
  font-size: 13px;
  line-height: 1.6;
  color: var(--text-secondary);
}

.section {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.sub-check {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 12px;
  border-radius: 8px;
  background: var(--darker-bg);
  border: 1px solid var(--border);
}

.sub-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.sub-label {
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
}

.impl-badge {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 10px;
  border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
  color: var(--accent);
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  white-space: nowrap;

  &.on {
    border-color: color-mix(in srgb, var(--secondary) 45%, transparent);
    color: var(--secondary);
    background: color-mix(in srgb, var(--secondary) 10%, transparent);
  }
}

.pending-note {
  margin: 0;
  font-size: 12px;
  color: var(--accent);
}

.section-title {
  font-size: 12px;
  color: var(--text-secondary);
  opacity: 0.75;
}

.factor-list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;

  li {
    display: flex;
    align-items: baseline;
    flex-wrap: wrap;
    gap: 8px;
    font-size: 12px;
  }
}

.factor-label {
  color: var(--text);
  min-width: 96px;
}

.factor-value {
  color: var(--primary);
  font-family: 'Consolas', 'Courier New', monospace;
}

.factor-note {
  color: var(--text-secondary);
  opacity: 0.7;
  font-size: 11px;
}

.metric-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.metric-chip {
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 4px;
  background: var(--darker-bg);
  border: 1px solid var(--border);
  color: var(--text-secondary);
}

.verdict-text {
  margin: 0;
  font-size: 12px;
  line-height: 1.6;
  color: var(--text-secondary);
}

.backend-need {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  padding: 8px 10px;
  border-radius: 6px;
  font-size: 12px;
  line-height: 1.6;
  color: var(--accent);
  background: color-mix(in srgb, var(--accent) 8%, transparent);
  border: 1px solid color-mix(in srgb, var(--accent) 30%, transparent);

  i {
    margin-top: 2px;
    flex-shrink: 0;
  }
}

.result-placeholder {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 18px;
  border: 1px dashed var(--border);
  border-radius: 8px;
  color: var(--text-secondary);
  font-size: 12px;
  background: var(--darker-bg);

  i {
    font-size: 22px;
    opacity: 0.4;
  }
}
</style>
