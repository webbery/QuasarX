<template>
<div class="components-panel">
    <!-- 按分类动态生成 -->
    <div v-for="cat in categories" :key="cat" class="category">
        <div class="category-title" @click="toggleCategory(cat)">
            <i :class="categoryIcons[cat]"></i>
            <span>{{ categoryLabels[cat] }}</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories[cat] }"></i>
        </div>
        <div class="components-list" v-show="openCategories[cat]">
            <div
                v-for="node in nodesByCategory[cat]"
                :key="node.id"
                class="component-card"
                :class="categoryClassMap[cat]"
                draggable="true"
                @dragstart="onDragStart($event, node.id)"
                :title="node.description"
            >
                <div class="component-title">{{ node.label }}</div>
            </div>
        </div>
    </div>
</div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
import { getAllCategories, getNodesByCategory } from '@/lib/nodes'
import { CATEGORY_ICONS } from '@/lib/nodes/types'

// 分类到图标的映射（从类型定义导入）
const categoryIcons = CATEGORY_ICONS

// 分类显示名称
const categoryLabels = {
  input: '输入节点',
  process: '处理节点',
  signal: '信号生成',
  execution: '执行交易',
  ml: 'AI 模型',
  risk: '风控保护',
  utility: '工具节点',
}

// 分类到 CSS 类的映射
const categoryClassMap = {
  input: 'input-node',
  process: 'process-node',
  signal: 'signal-node',
  execution: 'execution-node',
  ml: 'ml-node',
  risk: 'risk-node',
  utility: 'utility-node',
}

// 从注册表动态获取分类和节点
const categories = ref([])
const nodesByCategory = reactive({})

// 展开状态
const openCategories = reactive({})

onMounted(async () => {
  // 导入注册表
  const { getAllCategories, getNodesByCategory } = await import('@/lib/nodes')
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

const onDragStart = (event, nodeId) => {
  if (event.dataTransfer) {
    event.dataTransfer.setData('application/vueflow', nodeId)
    event.dataTransfer.effectAllowed = 'move'
  }
}
</script>

<style scoped>
.components-panel {
    background-color: var(--panel-bg);
    border-left: 1px solid var(--border);
    overflow-y: auto;
    box-shadow: -2px 0 5px rgba(0, 0, 0, 0.2);
    height: calc(100vh - 100px);
    max-height: calc(100vh - 100px);

    scrollbar-width: thin;
    scrollbar-color: var(--primary) transparent;
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

.category {
    margin-bottom: 15px;
}

.category-title {
    font-size: 1.1rem;
    font-weight: 600;
    color: var(--text);
    margin-bottom: 12px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    cursor: pointer;
    padding: 8px 12px;
    border-radius: 6px;
    transition: all 0.3s ease;
    user-select: none;
}

.category-title:hover {
    background: rgba(41, 98, 255, 0.1);
}

.category-title i:first-child {
    margin-right: 8px;
    color: var(--primary);
}

.category-title span {
    flex: 1;
}

.arrow {
    transition: transform 0.3s ease;
    font-size: 0.8rem;
    color: var(--text-secondary);
}

.rotate-180 {
    transform: rotate(180deg);
}

.components-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
    margin-bottom: 10px;
}

.component-card {
    background: rgba(41, 98, 255, 0.05);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 5px;
    cursor: grab;
    transition: all 0.3s ease;
}

.component-card:hover {
    background: rgba(41, 98, 255, 0.1);
    border-color: var(--primary);
    transform: translateY(-2px);
    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
}

.component-title {
    font-weight: 600;
    color: var(--text);
    font-size: 0.95rem;
}

/* 节点类型特定样式 */
.input-node .component-icon {
    background: rgba(0, 200, 83, 0.1);
    color: var(--secondary);
}

.output-node .component-icon {
    background: rgba(255, 109, 0, 0.1);
    color: var(--accent);
}

.signal-node .component-icon {
    background: rgba(41, 98, 255, 0.1);
    color: var(--primary);
}

.risk-node .component-icon {
    background: rgba(156, 39, 176, 0.1);
    color: #9c27b0;
}

.process-node .component-icon {
    background: rgba(76, 175, 80, 0.1);
    color: #4caf50;
}

.ml-node .component-icon {
    background: rgba(233, 30, 99, 0.1);
    color: #e91e63;
}

.execution-node .component-icon {
    background: rgba(255, 109, 0, 0.1);
    color: var(--accent);
}

.utility-node .component-icon {
    background: rgba(255, 152, 0, 0.1);
    color: #ff9800;
}
</style>
