<template>
<div class="components-panel">
    <!-- 1a 搜索框 -->
    <div class="search-box">
        <i class="fas fa-search search-icon"></i>
        <input
            v-model="searchQuery"
            type="text"
            class="search-input"
            placeholder="搜索节点（名称/描述/类型）"
        />
        <button
            v-if="searchQuery"
            class="search-clear"
            @click="searchQuery = ''"
            title="清空"
        >×</button>
    </div>

    <!-- 按分类动态生成 -->
    <div v-for="cat in displayedCategories" :key="cat" class="category">
        <div class="category-title" @click="toggleCategory(cat)">
            <i :class="categoryIcons[cat] || 'fas fa-cube'"></i>
            <span>{{ categoryLabels[cat] }}</span>
            <span class="cat-count">{{ filteredByCategory[cat]?.length || 0 }}</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories[cat] }"></i>
        </div>
        <div class="components-list" v-show="openCategories[cat]">
            <div
                v-for="node in filteredByCategory[cat]"
                :key="node.id"
                class="component-card"
                draggable="true"
                @dragstart="onDragStart($event, node.id)"
            >
                <!-- 2a 图标 -->
                <div class="card-icon" :class="`ic-${cat}`">
                    <i :class="node.icon || categoryIcons[cat] || 'fas fa-cube'"></i>
                </div>

                <!-- 2a 主体：label + 副标题 -->
                <div class="card-body">
                    <div class="card-label">{{ node.label }}</div>
                    <div class="card-subtitle">{{ summarize(node.description) }}</div>
                </div>

                <!-- 2a IO 点 -->
                <div class="card-io">
                    <span
                        v-if="node.hasInput !== false"
                        class="io-dot io-in"
                        :class="{ active: node.inputs.length > 0 }"
                        title="输入"
                    ></span>
                    <span
                        v-if="node.hasOutput !== false"
                        class="io-dot io-out"
                        :class="{ active: node.outputs.length > 0 }"
                        title="输出"
                    ></span>
                </div>

                <!-- 3a 富 tooltip -->
                <div class="card-tooltip">
                    <div class="tt-header">
                        <i :class="node.icon || categoryIcons[cat] || 'fas fa-cube'"></i>
                        <span>{{ node.label }}</span>
                        <span class="tt-nodetype">{{ node.nodeType }}</span>
                    </div>
                    <div class="tt-desc">{{ node.description }}</div>
                    <div v-if="node.inputs.length" class="tt-row">
                        <span class="tt-key">输入</span>
                        <span class="tt-vals">
                            <span v-for="i in node.inputs" :key="i" class="chip chip-in">{{ i }}</span>
                        </span>
                    </div>
                    <div v-if="node.outputs.length" class="tt-row">
                        <span class="tt-key">输出</span>
                        <span class="tt-vals">
                            <span v-for="o in node.outputs" :key="o" class="chip chip-out">{{ o }}</span>
                        </span>
                    </div>
                    <div class="tt-row tt-params">
                        <span class="tt-key">参数</span>
                        <span class="tt-vals">{{ node.params.length }} 个：{{ paramNames(node) }}</span>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <!-- 搜索无结果 -->
    <div v-if="searchQuery && totalFilteredCount === 0" class="empty-state">
        <i class="fas fa-search"></i>
        <div>没有匹配的节点</div>
    </div>

    <!-- EMD 出边 IMF 编号属性面板（点击 EMD 出边后显示在底部）-->
    <EdgeImfSelector
      v-if="clickedEdge"
      :edge="clickedEdge"
      :num-imfs="imfPanelNumImfs"
      :target-node-label="imfPanelTargetLabel"
      @set="handleSetEdgeImf"
    />
</div>
</template>

<script setup>
import { ref, reactive, onMounted, computed } from 'vue'
import EdgeImfSelector from './flow/EdgeImfSelector.vue'

const props = defineProps({
  clickedEdge: {
    type: Object,
    default: null
  },
  flowNodes: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['set-edge-imf'])

const imfPanelNumImfs = computed(() => {
  if (!props.clickedEdge) return 5

  const sourceNode = props.flowNodes.find(n => n.id === props.clickedEdge.source)
  const param = sourceNode?.data?.params?.['numIMFs'] ?? sourceNode?.data?.params?.['IMF 数量']
  const numIMFs = param?.value ?? param ?? 5
  const count = Number(numIMFs)
  return Number.isInteger(count) && count > 0 ? Math.min(count, 20) : 5
})

const imfPanelTargetLabel = computed(() => {
  if (!props.clickedEdge) return ''
  const targetNode = props.flowNodes.find(n => n.id === props.clickedEdge.target)
  return targetNode?.data?.label || `节点 ${props.clickedEdge.target}`
})

const handleSetEdgeImf = (newIndex) => {
  if (!props.clickedEdge) return
  emit('set-edge-imf', {
    edgeId: props.clickedEdge.id,
    newIndex
  })
}

// 分类显示名称
const categoryLabels = {
  input: '输入节点',
  process: '处理节点',
  signal: '信号生成',
  execution: '执行交易',
  ml: 'AI 模型',
  risk: '风控保护',
  utility: '工具节点',
  causal: '因果推理',
}

// 从注册表动态获取分类和节点
const categories = ref([])
const nodesByCategory = reactive({})

// 展开状态
const openCategories = reactive({})

// 1a 搜索状态
const searchQuery = ref('')

// 2a 分类图标（与 registry types.ts 的 CATEGORY_ICONS 保持一致）
const categoryIcons = {
  input:    'fas fa-plug',
  process:  'fas fa-cogs',
  signal:   'fas fa-bolt',
  execution:'fas fa-exchange-alt',
  ml:       'fas fa-brain',
  risk:     'fas fa-shield-alt',
  causal:   'fas fa-project-diagram',
  utility:  'fas fa-wrench',
}

let _searchNodes = () => []
let _getAllNodes = () => []

onMounted(async () => {
  // 导入注册表
  const mod = await import('@/lib/nodes')
  const { getAllCategories, getNodesByCategory } = mod
  _searchNodes = mod.searchNodes
  _getAllNodes = mod.getAllNodes
  const cats = getAllCategories()
  categories.value = cats

  for (const cat of cats) {
    nodesByCategory[cat] = getNodesByCategory(cat)
    openCategories[cat] = true // 默认全部展开
  }
})

const toggleCategory = (category) => {
  openCategories[category] = !openCategories[category]
}

// 1a: 搜索时跨分类平铺所有匹配节点；非搜索时按原分类分组
const filteredByCategory = computed(() => {
  if (!searchQuery.value.trim()) {
    return nodesByCategory
  }
  const q = searchQuery.value.trim().toLowerCase()
  const result = {}
  for (const cat of categories.value) {
    result[cat] = (nodesByCategory[cat] || []).filter(n =>
      n.label.toLowerCase().includes(q) ||
      n.nodeType.toLowerCase().includes(q) ||
      n.description.toLowerCase().includes(q)
    )
  }
  return result
})

const displayedCategories = computed(() => categories.value)

const totalFilteredCount = computed(() => {
  return Object.values(filteredByCategory.value).reduce(
    (sum, arr) => sum + (arr?.length || 0), 0
  )
})

const onDragStart = (event, nodeId) => {
  if (event.dataTransfer) {
    event.dataTransfer.setData('application/vueflow', nodeId)
    event.dataTransfer.effectAllowed = 'move'
  }
}

// 2a: 描述摘要（取第一个句号前）
const summarize = (desc) => {
  if (!desc) return ''
  const firstDot = desc.search(/[。！？.!?]/)
  if (firstDot > 0 && firstDot < 50) {
    return desc.slice(0, firstDot + 1)
  }
  return desc.length > 38 ? desc.slice(0, 38) + '…' : desc
}

// 3a: tooltip 用参数名列表
const paramNames = (node) => {
  const names = (node.params || []).map(p => p.label).filter(Boolean)
  if (names.length === 0) return '无'
  if (names.length <= 3) return names.join('、')
  return names.slice(0, 3).join('、') + ` 等`
}
</script>

<style scoped>
.components-panel {
    background-color: var(--panel-bg);
    border-left: 1px solid var(--border);
    overflow-y: auto;
    overflow-x: visible;
    box-shadow: -2px 0 5px rgba(0, 0, 0, 0.2);
    height: calc(100vh - 100px);
    max-height: calc(100vh - 100px);

    scrollbar-width: thin;
    scrollbar-color: var(--primary) transparent;
    padding: 8px;
}

.components-panel::-webkit-scrollbar {
    width: 6px;
}

.components-panel::-webkit-scrollbar-track {
    background: transparent;
}

.components-panel::-webkit-scrollbar-thumb {
    background-color: var(--primary);
    border-radius: 3px;
}

.components-panel::-webkit-scrollbar-thumb:hover {
    background-color: var(--primary-dark);
}

/* ───── 1a 搜索框 ───── */
.search-box {
    position: sticky;
    top: 0;
    z-index: 10;
    display: flex;
    align-items: center;
    background: rgba(26, 34, 54, 0.95);
    backdrop-filter: blur(6px);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 4px 8px;
    margin-bottom: 8px;
}

.search-icon {
    color: var(--text-secondary);
    font-size: 0.8rem;
    margin-right: 6px;
}

.search-input {
    flex: 1;
    background: transparent;
    border: none;
    color: var(--text);
    font-size: 0.8rem;
    outline: none;
    padding: 4px 0;
}

.search-input::placeholder {
    color: var(--text-secondary);
}

.search-clear {
    background: transparent;
    border: none;
    color: var(--text-secondary);
    font-size: 1.1rem;
    line-height: 1;
    cursor: pointer;
    padding: 0 4px;
}

.search-clear:hover {
    color: var(--text);
}

/* ───── 分类标题 ───── */
.category {
    margin-bottom: 8px;
}

.category-title {
    font-size: 0.82rem;
    font-weight: 600;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 6px;
    cursor: pointer;
    padding: 6px 8px;
    border-radius: 4px;
    transition: all 0.2s ease;
    user-select: none;
}

.category-title:hover {
    background: rgba(41, 98, 255, 0.1);
}

.category-title span {
    flex: 0 0 auto;
}

.cat-count {
    margin-left: 2px;
    font-size: 0.7rem;
    color: var(--text-secondary);
    background: rgba(255,255,255,0.06);
    padding: 1px 6px;
    border-radius: 8px;
    min-width: 18px;
    text-align: center;
}

.arrow {
    margin-left: auto;
    transition: transform 0.2s ease;
    font-size: 0.65rem;
    color: var(--text-secondary);
}

.rotate-180 {
    transform: rotate(180deg);
}

.components-list {
    display: flex;
    flex-direction: column;
    gap: 4px;
    margin-bottom: 4px;
    padding: 0 4px;
}

/* ───── 2a 卡片 ───── */
.component-card {
    position: relative;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 10px;
    cursor: grab;
    transition: all 0.15s ease;
    border-radius: 6px;
    background: rgba(255,255,255,0.025);
    border: 1px solid transparent;
}

.component-card:hover {
    background: rgba(41, 98, 255, 0.12);
    border-color: rgba(41, 98, 255, 0.3);
}

.component-card:active {
    cursor: grabbing;
}

.card-icon {
    width: 30px;
    height: 30px;
    border-radius: 7px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 13px;
    flex-shrink: 0;
}

.card-body {
    flex: 1;
    min-width: 0;
    overflow: hidden;
}

.card-label {
    font-size: 0.8rem;
    color: var(--text);
    font-weight: 500;
    line-height: 1.2;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.card-subtitle {
    font-size: 0.68rem;
    color: var(--text-secondary);
    margin-top: 2px;
    line-height: 1.2;
    overflow: hidden;
    text-overflow: ellipsis;
    display: -webkit-box;
    -webkit-line-clamp: 1;
    -webkit-box-orient: vertical;
}

.card-io {
    display: flex;
    align-items: center;
    gap: 3px;
    flex-shrink: 0;
}

.io-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: rgba(255,255,255,0.1);
    border: 1px solid rgba(255,255,255,0.2);
    transition: all 0.15s;
}

.io-dot.io-in.active {
    background: #00bcd4;
    border-color: #00bcd4;
    box-shadow: 0 0 4px rgba(0,188,212,0.4);
}

.io-dot.io-out.active {
    background: #ff9800;
    border-color: #ff9800;
    box-shadow: 0 0 4px rgba(255,152,0,0.4);
}

/* ───── 2a 分类图标色 ───── */
.ic-input    { background: rgba(0,188,212,0.18); color: #4dd0e1; }
.ic-process  { background: rgba(41,98,255,0.18); color: #6ea8ff; }
.ic-signal   { background: rgba(255,152,0,0.18); color: #ffb74d; }
.ic-execution{ background: rgba(76,175,80,0.18); color: #81c784; }
.ic-ml       { background: rgba(156,39,176,0.18); color: #ce93d8; }
.ic-risk     { background: rgba(244,67,54,0.18); color: #ef9a9a; }
.ic-causal   { background: rgba(233,30,99,0.18); color: #f48fb1; }
.ic-utility  { background: rgba(154,163,178,0.18); color: #cfd8dc; }

/* ───── 3a 富 tooltip ───── */
.card-tooltip {
    position: absolute;
    left: calc(100% + 8px);
    top: 50%;
    transform: translateY(-50%);
    width: 280px;
    background: #1f2937;
    color: #e0e0e0;
    border: 1px solid rgba(74,85,104,0.4);
    border-radius: 6px;
    padding: 10px 12px;
    font-size: 12px;
    line-height: 1.45;
    box-shadow: 0 6px 24px rgba(0,0,0,0.5);
    opacity: 0;
    visibility: hidden;
    transition: opacity 0.15s ease, visibility 0.15s ease;
    z-index: 100;
    pointer-events: none;
}

.component-card:hover .card-tooltip {
    opacity: 1;
    visibility: visible;
}

.tt-header {
    display: flex;
    align-items: center;
    gap: 6px;
    font-weight: 600;
    color: #fff;
    margin-bottom: 6px;
    padding-bottom: 6px;
    border-bottom: 1px solid rgba(255,255,255,0.08);
}

.tt-header i {
    color: #6ea8ff;
}

.tt-nodetype {
    margin-left: auto;
    font-size: 10px;
    color: #9aa3b2;
    font-family: ui-monospace, monospace;
    background: rgba(255,255,255,0.06);
    padding: 1px 5px;
    border-radius: 3px;
}

.tt-desc {
    color: #cfd8dc;
    margin-bottom: 8px;
    font-size: 11.5px;
}

.tt-row {
    display: flex;
    align-items: flex-start;
    gap: 6px;
    margin-top: 4px;
    font-size: 11px;
}

.tt-key {
    color: #9aa3b2;
    flex-shrink: 0;
    width: 32px;
}

.tt-vals {
    display: flex;
    flex-wrap: wrap;
    gap: 3px;
    color: #e0e0e0;
}

.chip {
    background: rgba(255,255,255,0.08);
    border: 1px solid rgba(255,255,255,0.1);
    padding: 1px 6px;
    border-radius: 3px;
    font-size: 10.5px;
    font-family: ui-monospace, monospace;
}

.chip-in { color: #4dd0e1; border-color: rgba(0,188,212,0.3); }
.chip-out { color: #ffb74d; border-color: rgba(255,152,0,0.3); }

.tt-params .tt-vals {
    color: #cfd8dc;
    font-family: inherit;
}

/* 空状态 */
.empty-state {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 8px;
    padding: 32px 16px;
    color: var(--text-secondary);
    font-size: 0.85rem;
}

.empty-state i {
    font-size: 2rem;
    opacity: 0.4;
}
</style>