<template>
<div class="components-panel">
    <!-- 按分类动态生成 -->
    <div v-for="cat in categories" :key="cat" class="category">
        <div class="category-title" @click="toggleCategory(cat)">
            <span>{{ categoryLabels[cat] }}</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories[cat] }"></i>
        </div>
        <div class="components-list" v-show="openCategories[cat]">
            <div
                v-for="node in nodesByCategory[cat]"
                :key="node.id"
                class="component-item"
                draggable="true"
                @dragstart="onDragStart($event, node.id)"
                :title="node.description"
            >
                <span class="component-name">{{ node.label }}</span>
            </div>
        </div>
    </div>
</div>
</template>

<script setup>
import { ref, reactive, onMounted } from 'vue'

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
    margin-bottom: 8px;
}

.category-title {
    font-size: 0.85rem;
    font-weight: 600;
    color: var(--text);
    display: flex;
    align-items: center;
    justify-content: space-between;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 4px;
    transition: all 0.2s ease;
    user-select: none;
}

.category-title:hover {
    background: rgba(41, 98, 255, 0.1);
}

.category-title span {
    flex: 1;
}

.arrow {
    transition: transform 0.2s ease;
    font-size: 0.7rem;
    color: var(--text-secondary);
}

.rotate-180 {
    transform: rotate(180deg);
}

.components-list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    margin-bottom: 4px;
    padding-left: 8px;
}

.component-item {
    display: flex;
    align-items: center;
    padding: 3px 8px;
    cursor: grab;
    transition: all 0.15s ease;
    border-radius: 3px;
}

.component-item:hover {
    background: rgba(41, 98, 255, 0.15);
}

.component-item:active {
    cursor: grabbing;
}

.component-name {
    font-size: 0.75rem;
    color: #2962ff;
    font-weight: 500;
}
</style>
