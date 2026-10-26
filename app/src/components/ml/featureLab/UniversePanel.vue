<template>
  <div class="universe">
    <div class="u-head">
      <span class="u-title">标的池</span>
      <span class="u-count" :class="{ warn: modelValue.length < minNames }">
        {{ modelValue.length }} 只
      </span>
      <button class="u-link" @click="pasteOpen = !pasteOpen">粘贴导入</button>
    </div>

    <input v-model="query" class="u-search" placeholder="搜索代码 / 名称…" />

    <div v-if="pasteOpen" class="u-paste">
      <textarea v-model="pasteText" rows="3"
                placeholder="sh.600000, sz.000001&#10;每行一个或用逗号分隔"></textarea>
      <div class="u-paste-actions">
        <button class="u-btn" @click="applyPaste">导入</button>
        <button class="u-btn ghost" @click="pasteOpen = false">取消</button>
      </div>
    </div>

    <div class="u-list">
      <label v-for="s in filtered" :key="s.symbol" class="u-row">
        <input type="checkbox" :checked="selected.has(s.symbol)" @change="toggle(s.symbol)" />
        <span class="u-sym">{{ s.symbol }}</span>
        <span class="u-name">{{ s.name }}</span>
      </label>
      <div v-if="!filtered.length" class="u-empty">
        {{ loading ? '加载股票列表…' : '没有匹配的标的' }}
      </div>
    </div>

    <div class="u-foot">
      <button class="u-link" @click="selectFiltered">全选当前</button>
      <button class="u-link" @click="clearAll">清空</button>
      <span v-if="modelValue.length < minNames" class="u-warn">
        横截面 IC 需要单日 ≥{{ minNames }} 只
      </span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import { toGraphSymbol } from '@/lib/symbol'

interface StockItem { symbol: string; name: string }

const props = defineProps<{ modelValue: string[]; minNames: number }>()
const emit = defineEmits<{ (e: 'update:modelValue', v: string[]): void }>()

const stocks = ref<StockItem[]>([])
const loading = ref(false)
const query = ref('')
const pasteOpen = ref(false)
const pasteText = ref('')

const selected = computed(() => new Set(props.modelValue))

const filtered = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return stocks.value
  return stocks.value.filter(s =>
    s.symbol.toLowerCase().includes(q) || (s.name || '').toLowerCase().includes(q),
  )
})

function setSelected(next: Set<string>) {
  emit('update:modelValue', [...next])
}

function toggle(symbol: string) {
  const next = new Set(selected.value)
  next.has(symbol) ? next.delete(symbol) : next.add(symbol)
  setSelected(next)
}

function selectFiltered() {
  const next = new Set(selected.value)
  for (const s of filtered.value) next.add(s.symbol)
  setSelected(next)
}

function clearAll() {
  emit('update:modelValue', [])
}

function applyPaste() {
  const known = new Set(stocks.value.map(s => s.symbol))
  const parts = pasteText.value.split(/[\s,，;；]+/).map(s => toGraphSymbol(s)).filter(Boolean)
  const unknown: string[] = []
  const next = new Set(selected.value)
  for (const p of parts) {
    if (known.size === 0 || known.has(p)) next.add(p)
    else unknown.push(p)
  }
  setSelected(next)
  if (unknown.length) {
    ElMessage.warning(`未在股票列表中找到：${unknown.slice(0, 5).join(', ')}${unknown.length > 5 ? ' …' : ''}`)
  }
  pasteText.value = ''
  pasteOpen.value = false
}

onMounted(async () => {
  loading.value = true
  try {
    const resp = await axios.get('/v0/stocks/simple')
    const raw = resp.data?.stocks ?? resp.data ?? []
    stocks.value = (Array.isArray(raw) ? raw : [])
      .map((s: any) => ({
        // 服务端 /v0/stocks/simple 返回的是裸 6 位代码，这里统一补成 sh./sz. 前缀
        symbol: toGraphSymbol(String(s.symbol ?? s.code ?? '')),
        name: String(s.name ?? ''),
      }))
      .filter(s => s.symbol)
  } catch (e: any) {
    ElMessage.error(`股票列表加载失败: ${e?.message || e}`)
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.universe { padding: 10px 12px 6px; }

.u-head {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 7px;
}

.u-title { font-size: 11px; font-weight: 600; color: #b9c7d5; letter-spacing: 0.5px; }
.u-count { font-size: 10px; color: #6b7f94; font-family: 'SF Mono', Consolas, monospace; }
.u-count.warn { color: #fbbf24; }

.u-link {
  background: none;
  border: none;
  padding: 0;
  margin-left: auto;
  color: #5b8ff9;
  font-family: inherit;
  font-size: 10.5px;
  cursor: pointer;
}
.u-link:hover { color: #93c5fd; }

.u-search {
  width: 100%;
  box-sizing: border-box;
  padding: 4px 7px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 3px;
  color: #dbe4ee;
  font-family: inherit;
  font-size: 11px;
  outline: none;
}
.u-search:focus { border-color: #38bdf8; }

.u-paste { margin-top: 7px; }
.u-paste textarea {
  width: 100%;
  box-sizing: border-box;
  resize: vertical;
  padding: 5px 7px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 3px;
  color: #dbe4ee;
  font-family: 'SF Mono', Consolas, monospace;
  font-size: 10.5px;
  outline: none;
}
.u-paste-actions { display: flex; gap: 6px; margin-top: 5px; }

.u-btn {
  padding: 3px 10px;
  background: rgba(56, 189, 248, 0.13);
  border: 1px solid rgba(56, 189, 248, 0.45);
  border-radius: 3px;
  color: #a5e0ff;
  font-family: inherit;
  font-size: 10.5px;
  cursor: pointer;
}
.u-btn.ghost { background: transparent; border-color: rgba(74, 85, 104, 0.4); color: #94a3b8; }

.u-list {
  margin-top: 7px;
  max-height: 168px;
  overflow: auto;
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 3px;
  background: rgba(10, 18, 32, 0.4);
}
.u-list::-webkit-scrollbar { width: 5px; }
.u-list::-webkit-scrollbar-thumb { background: #22303f; }

.u-row {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 3px 7px;
  font-size: 10.5px;
  color: #cbd5e1;
  cursor: pointer;
}
.u-row:hover { background: rgba(56, 189, 248, 0.05); }
.u-row input { accent-color: #3b82f6; }

.u-sym { font-family: 'SF Mono', Consolas, monospace; color: #e2e8f0; }
.u-name { color: #64748b; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

.u-empty { padding: 14px 8px; text-align: center; font-size: 10.5px; color: #4f5d6d; }

.u-foot {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 6px;
}
.u-foot .u-link { margin-left: 0; }
.u-warn { margin-left: auto; font-size: 9.5px; color: #fbbf24; }
</style>
