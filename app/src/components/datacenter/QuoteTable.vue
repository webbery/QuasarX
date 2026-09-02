<template>
  <div class="quote-table-wrap">
    <div class="table-meta">
      <span class="meta-label">{{ freqLabel }}</span>
      <span class="meta-count">{{ bars.length }} 根 K 线</span>
    </div>
    <div v-if="bars.length === 0" class="empty-tip small">
      <i class="fas fa-info-circle"></i> 暂无行情数据
    </div>
    <table v-else class="detail-table compact">
      <thead>
        <tr>
          <th>时间</th>
          <th class="num">开盘</th>
          <th class="num">最高</th>
          <th class="num">最低</th>
          <th class="num">收盘</th>
          <th class="num">成交量</th>
          <th v-if="deletable" class="col-action">操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(b, i) in pagedBars" :key="i">
          <td>{{ formatTime(b.datetime) }}</td>
          <td class="num">{{ formatPrice(b.open) }}</td>
          <td class="num">{{ formatPrice(b.high) }}</td>
          <td class="num">{{ formatPrice(b.low) }}</td>
          <td class="num">{{ formatPrice(b.close) }}</td>
          <td class="num">{{ formatVolume(b.volume) }}</td>
          <td v-if="deletable" class="col-action">
            <button class="row-del-btn" @click="onRowDelete(b)" title="删除该行 K 线">
              <i class="fas fa-trash"></i>
            </button>
          </td>
        </tr>
      </tbody>
    </table>
    <div v-if="bars.length > pageSize" class="pagination-row">
      <button class="pg-btn" :disabled="page === 1" @click="page = 1" title="首页">«</button>
      <button class="pg-btn" :disabled="page === 1" @click="page--" title="上一页">‹</button>
      <span class="pg-info">{{ page }} / {{ totalPages }}</span>
      <button class="pg-btn" :disabled="page === totalPages" @click="page++" title="下一页">›</button>
      <button class="pg-btn" :disabled="page === totalPages" @click="page = totalPages" title="末页">»</button>
      <span class="pg-jump">
        跳转
        <input
          type="number"
          :min="1"
          :max="totalPages"
          v-model.number="jumpPage"
          @keyup.enter="onJump"
          @blur="onJump"
        />
        页
      </span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import type { HistoryBar } from './composables/useSymbolDetailData'

const props = defineProps<{
  bars: HistoryBar[]
  freqLabel: string
  isDaily: boolean
  deletable?: boolean
  onDelete?: (bar: HistoryBar) => void
}>()

const page = ref(1)
const pageSize = 50
const jumpPage = ref(1)

// 倒序展示：最新数据在最上面
const reversed = computed(() => [...props.bars].reverse())
const totalPages = computed(() => Math.max(1, Math.ceil(reversed.value.length / pageSize)))
const pagedBars = computed(() => {
  const start = (page.value - 1) * pageSize
  return reversed.value.slice(start, start + pageSize)
})

watch(() => props.bars.length, () => { page.value = 1; jumpPage.value = 1 })
watch(totalPages, (n) => { if (page.value > n) page.value = n })

function onJump() {
  let target = Number(jumpPage.value)
  if (!isFinite(target) || target < 1) target = 1
  if (target > totalPages.value) target = totalPages.value
  page.value = target
  jumpPage.value = target
}

defineExpose({ page, totalPages, jumpToPage: (p: number) => { jumpPage.value = p; onJump() } })

function onRowDelete(bar: HistoryBar) {
  if (!props.onDelete) return
  if (!confirm(`确定删除 ${bar.datetime} 的 K 线？此操作不可恢复！`)) return
  props.onDelete(bar)
}

function formatTime(dt: string): string {
  if (!dt) return '—'
  return props.isDaily ? dt.slice(0, 10) : dt
}

function formatPrice(v: number): string {
  if (!isFinite(v)) return '—'
  return v.toFixed(2)
}

function formatVolume(v: number): string {
  if (!isFinite(v) || v === 0) return '—'
  if (v >= 1e8) return (v / 1e8).toFixed(2) + '亿'
  if (v >= 1e4) return (v / 1e4).toFixed(2) + '万'
  return v.toFixed(0)
}
</script>

<style scoped>
.quote-table-wrap {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.table-meta {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 11px;
  color: var(--text-secondary, #b0b0b0);
}

.meta-label {
  padding: 2px 6px;
  background: var(--bg-secondary, #2a2a2a);
  border-radius: 3px;
}

.detail-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 11px;
}

.detail-table th,
.detail-table td {
  padding: 4px 6px;
  text-align: left;
  border-bottom: 1px solid var(--border, #333);
}

.detail-table th {
  font-weight: 500;
  color: var(--text-secondary, #b0b0b0);
  background: var(--bg-secondary, #252525);
  position: sticky;
  top: 0;
}

.detail-table.compact th,
.detail-table.compact td {
  padding: 4px 6px;
  font-size: 11px;
}

.detail-table td.num,
.detail-table th.num {
  text-align: right;
  font-variant-numeric: tabular-nums;
}

.detail-table tbody tr:hover {
  background: var(--hover-bg, rgba(255, 255, 255, 0.03));
}

.pagination-row {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding-top: 6px;
}

.pg-btn {
  background: var(--bg-secondary, #2a2a2a);
  border: 1px solid var(--border, #333);
  color: var(--text-primary, #e0e0e0);
  padding: 2px 8px;
  border-radius: 3px;
  cursor: pointer;
  font-size: 12px;
}

.pg-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.pg-info {
  font-size: 11px;
  color: var(--text-secondary, #b0b0b0);
}

.pg-jump {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--text-secondary, #b0b0b0);
  margin-left: 6px;
}

.pg-jump input {
  width: 50px;
  padding: 2px 4px;
  background: var(--bg-secondary, #2a2a2a);
  border: 1px solid var(--border, #333);
  color: var(--text-primary, #e0e0e0);
  border-radius: 3px;
  font-size: 11px;
  text-align: center;
}

.pg-jump input:focus {
  outline: none;
  border-color: var(--primary, #4a9eff);
}

.pg-jump input::-webkit-inner-spin-button,
.pg-jump input::-webkit-outer-spin-button {
  -webkit-appearance: none;
  margin: 0;
}

.empty-tip.small {
  padding: 16px;
  text-align: center;
  color: var(--text-secondary, #888);
}

.col-action {
  width: 60px;
  text-align: center;
}

.row-del-btn {
  background: transparent;
  border: 1px solid rgba(245, 108, 108, 0.3);
  color: #f56c6c;
  padding: 2px 6px;
  border-radius: 3px;
  cursor: pointer;
  font-size: 10px;
  transition: all 0.15s;
}

.row-del-btn:hover {
  background: rgba(245, 108, 108, 0.15);
  border-color: #f56c6c;
}
</style>