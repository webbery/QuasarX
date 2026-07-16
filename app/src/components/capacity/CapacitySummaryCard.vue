<template>
  <div class="capacity-summary-card">
    <div class="summary-grid">
      <div class="summary-item">
        <span class="label">基准 Sharpe</span>
        <span class="value" :class="{ negative: baseline.sharpe < 0 }">{{ baseline.sharpe.toFixed(2) }}</span>
      </div>
      <div class="summary-item">
        <span class="label">基准总收益</span>
        <span class="value" :class="{ negative: baseline.total_return < 0 }">{{ formatPct(baseline.total_return) }}</span>
      </div>
      <div class="summary-item">
        <span class="label">基准最大回撤</span>
        <span class="value negative">{{ formatPct(baseline.max_drawdown) }}</span>
      </div>
      <div class="summary-item">
        <span class="label">交易笔数</span>
        <span class="value">{{ baseline.n_trades }}</span>
      </div>
      <div class="summary-item highlight">
        <span class="label">容量上限 (20%衰减)</span>
        <span class="value primary">{{ formatCapital(summary.capacity_20pct) }}</span>
      </div>
      <div class="summary-item highlight">
        <span class="label">容量上限 (50%衰减)</span>
        <span class="value warning">{{ formatCapital(summary.capacity_50pct) }}</span>
      </div>
      <div class="summary-item">
        <span class="label">瓶颈标的</span>
        <span class="value">{{ summary.bottleneck_symbol || '—' }}</span>
      </div>
      <div class="summary-item">
        <span class="label">瓶颈 ADV</span>
        <span class="value">{{ formatCapital(summary.bottleneck_adv) }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  baseline: { sharpe: number; total_return: number; max_drawdown: number; win_rate: number; n_trades: number }
  summary: { capacity_20pct: number; capacity_50pct: number; bottleneck_symbol: string; bottleneck_adv: number }
}>()

function formatPct(v: number): string {
  return (v * 100).toFixed(2) + '%'
}

function formatCapital(v: number): string {
  if (v <= 0) return '—'
  if (v >= 1e8) return (v / 1e8).toFixed(2) + '亿'
  if (v >= 1e4) return (v / 1e4).toFixed(0) + '万'
  return v.toFixed(0)
}
</script>

<style scoped>
.capacity-summary-card {
  background: var(--bg-secondary, #1e1e2e);
  border: 1px solid var(--border-color, #333);
  border-radius: 8px;
  padding: 16px;
}
.summary-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}
.summary-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.summary-item .label {
  font-size: 11px;
  color: var(--text-secondary, #888);
}
.summary-item .value {
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary, #e0e0e0);
}
.summary-item.highlight {
  background: var(--bg-tertiary, #2a2a3e);
  padding: 8px;
  border-radius: 6px;
}
.value.primary { color: #4fc3f7; }
.value.warning { color: #ffb74d; }
.value.negative { color: #ef5350; }
</style>
