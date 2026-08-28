<template>
  <div class="model-detail">
    <div class="detail-grid">
      <!-- 训练配置 -->
      <div class="detail-group">
        <h4 class="group-title">训练配置</h4>
        <div class="kv-list">
          <div class="kv"><span class="k">模型类型</span><span class="v">{{ meta?.model_type || 'xgboost' }}</span></div>
          <div class="kv"><span class="k">目标函数</span><span class="v">{{ meta?.objective || '—' }}</span></div>
          <div class="kv"><span class="k">分类数</span><span class="v">{{ meta?.num_class ?? '—' }}</span></div>
          <div class="kv"><span class="k">验证比例</span><span class="v">{{ meta?.val_ratio ?? '—' }}</span></div>
          <div class="kv"><span class="k">测试比例</span><span class="v">{{ meta?.test_ratio ?? '—' }}</span></div>
          <div class="kv"><span class="k">特征数</span><span class="v">{{ meta?.n_features ?? '—' }}</span></div>
          <div class="kv"><span class="k">{{ (meta?.n_val ?? 0) > 0 ? '训练/验证/测试' : '训练/测试' }}</span><span class="v">{{ meta?.n_train ?? '—' }}{{ (meta?.n_val ?? 0) > 0 ? ` / ${meta.n_val}` : '' }} / {{ meta?.n_test ?? '—' }}</span></div>
        </div>
      </div>

      <!-- 标签配置 -->
      <div class="detail-group">
        <h4 class="group-title">标签配置</h4>
        <div class="kv-list">
          <div class="kv"><span class="k">类型</span><span class="v">{{ meta?.label?.type || '—' }}</span></div>
          <div class="kv"><span class="k">形状</span><span class="v">{{ meta?.label?.shape || '—' }}</span></div>
          <div class="kv"><span class="k">周期</span><span class="v">{{ meta?.label?.period ?? '—' }}</span></div>
          <div class="kv"><span class="k">vol_k</span><span class="v">{{ meta?.label?.vol_k ?? '—' }}</span></div>
          <div class="kv"><span class="k">来源</span><span class="v mono">{{ meta?.label?.source || '—' }}</span></div>
        </div>
      </div>

      <!-- 日期范围 -->
      <div class="detail-group">
        <h4 class="group-title">数据范围</h4>
        <div class="kv-list">
          <div class="kv"><span class="k">开始</span><span class="v">{{ meta?.date_range?.start || '—' }}</span></div>
          <div class="kv"><span class="k">结束</span><span class="v">{{ meta?.date_range?.end || '—' }}</span></div>
          <div class="kv"><span class="k">频率</span><span class="v">{{ meta?.date_range?.frequency || '—' }}</span></div>
        </div>
      </div>

      <!-- 评估指标 -->
      <div v-if="meta?.eval_metrics" class="detail-group">
        <h4 class="group-title">评估指标</h4>
        <div class="kv-list">
          <div v-for="(val, key) in meta.eval_metrics" :key="key" class="kv">
            <span class="k">{{ key }}</span>
            <span class="v">{{ typeof val === 'number' ? val.toFixed(4) : val }}</span>
          </div>
        </div>
      </div>

      <!-- 超参数 -->
      <div v-if="meta?.params && Object.keys(meta.params).length" class="detail-group">
        <h4 class="group-title">超参数</h4>
        <div class="kv-list">
          <div v-for="(val, key) in meta.params" :key="key" class="kv">
            <span class="k">{{ key }}</span>
            <span class="v">{{ val }}</span>
          </div>
        </div>
      </div>

      <!-- 特征列表 -->
      <div v-if="meta?.features?.length" class="detail-group full-width">
        <h4 class="group-title">特征列表 ({{ meta.features.length }})</h4>
        <div class="feature-tags">
          <span v-for="f in meta.features" :key="f" class="feature-tag">{{ f }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{ model: { path: string; filename: string; meta: any } }>()
const meta = computed(() => props.model.meta)
</script>

<style scoped>
.model-detail {
  padding: 12px 16px 16px;
  background: rgba(10, 18, 32, 0.5);
  border-top: 1px solid rgba(74, 85, 104, 0.15);
}

.detail-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 16px;
}

.detail-group.full-width { grid-column: 1 / -1; }

.group-title {
  font-size: 11px;
  letter-spacing: 1.5px;
  color: #5b8ff9;
  font-weight: 600;
  text-transform: uppercase;
  margin: 0 0 8px;
  padding-bottom: 4px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
}

.kv-list { display: flex; flex-direction: column; gap: 4px; }
.kv {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  gap: 8px;
  font-size: 12px;
  padding: 2px 0;
}
.k { color: #94a3b8; white-space: nowrap; }
.v { color: #e2e8f0; text-align: right; font-family: 'SF Mono', 'Consolas', monospace; font-size: 11.5px; }
.v.mono { font-size: 10.5px; }

.feature-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.feature-tag {
  font-size: 11px;
  padding: 2px 8px;
  background: rgba(91, 143, 249, 0.1);
  color: #93c5fd;
  border-radius: 3px;
  font-family: 'SF Mono', 'Consolas', monospace;
}
</style>
