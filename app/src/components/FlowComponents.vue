<template>
<div class="components-panel">
    <!-- 输入节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('input')">
            <i class="fas fa-download"></i>
            <span>输入节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.input }"></i>
        </div>
        <div class="components-list" v-show="openCategories.input">
            <div class="component-card input-node" draggable="true" @dragstart="onDragStart($event, 'data-source')" title="从外部获取数据，作为策略的基础输入">
                <div class="component-title">数据源</div> 
            </div>
        </div>
    </div>
    
    <!-- 输出节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('output')">
            <i class="fas fa-upload"></i>
            <span>输出节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.output }"></i>
        </div>
        <div class="components-list" v-show="openCategories.output">
            <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'signal-generation')" title="将输入数据转换为买卖信号">
                <div class="component-title">交易信号生成</div>
            </div>
            <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'signal-generation')" title="将输入数据下载成指定格式">
                <div class="component-title">调试</div>
            </div>
        </div>
    </div>
    
    <div class="category">
        <div class="category-title" @click="toggleCategory('strategy')">
            <i class="fas fa-chess-knight"></i>
            <span>AI节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.strategy }"></i>
        </div>
        <div v-show="openCategories.strategy">
            <!-- 神经网络模型节点 -->
                <div class="components-list" v-show="openSubcategories.neural">
                    <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'cnn')">
                        <div class="component-title">CNN</div>
                    </div>
                    
                    <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'lstm')">
                        <div class="component-title">LSTM</div>
                    </div>
                    <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'narx')">
                        <div class="component-title">NARX</div>
                    </div>
                    <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'xgboost')">
                            <div class="component-title">xgboost</div>
                    </div>
                    <div class="component-card strategy-node" draggable="true" @dragstart="onDragStart($event, 'xgboost')">
                            <div class="component-title">LLM</div>
                    </div>
                </div>
        </div>
    </div>

    <div class="category">
        <div class="category-title" @click="toggleCategory('signal')">
            <i class="fas fa-download"></i>
            <span>信号处理节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.input }"></i>
        </div>
        <div class="components-list" v-show="openCategories.input">
            <div class="component-card input-node" draggable="true" @dragstart="onDragStart($event, 'data-source')">
                <div class="component-title">EMD</div> 
            </div>
        </div>
    </div>

    <div class="category">
        <div class="category-title" @click="toggleCategory('signal')">
            <i class="fas fa-download"></i>
            <span>因果推理节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.input }"></i>
        </div>
        <div class="components-list" v-show="openCategories.input">
            <div class="component-card input-node" draggable="true" @dragstart="onDragStart($event, 'data-source')">
                <div class="component-title">HMM</div> 
            </div>
        </div>
    </div>
    
    <!-- 公式节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('operation')">
            <i class="fas fa-calculator"></i>
            <span>其他节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.operation }"></i>
        </div>
        <div v-show="openCategories.operation">
            <!-- 合并节点 -->
            <div class="components-list" v-show="openSubcategories.merge">
                <div class="component-card operation-node" draggable="true" @dragstart="onDragStart($event, 'node-merge')">
                    <div class="component-title">合并节点</div>
                </div>
                <div class="component-card operation-node" draggable="true" @dragstart="onDragStart($event, 'normalization')"
                    title="包含一些常见函数如MA、RSI等金融特征及归一化计算">
                    <div class="component-title">运算节点</div>
                </div>
            </div>
        </div>
        <div class="components-list" v-show="openCategories.input">
            <div class="component-card input-node" draggable="true" @dragstart="onDragStart($event, 'data-source')">
                <div class="component-title">统计检验</div> 
            </div>
        </div>
        <div class="components-list" v-show="openCategories.input">
            <div class="component-card input-node" draggable="true" @dragstart="onDragStart($event, 'data-python')">
                <div class="component-title">python脚本</div> 
            </div>
        </div>
    </div>
</div>
</template>

<script setup>
import { ref, reactive } from 'vue'

// 控制分类展开状态
const openCategories = reactive({
    input: true,
    output: true,
    strategy: true,
    featureNode: true,
    operation: true
})

// 控制子分类展开状态
const openSubcategories = reactive({
    neural: true,
    ml: true,
    feature: true,
    merge: true,
    normalization: true
})

// 切换分类展开状态
const toggleCategory = (category) => {
    openCategories[category] = !openCategories[category]
}

// 切换子分类展开状态
const toggleSubcategory = (subcategory) => {
    openSubcategories[subcategory] = !openSubcategories[subcategory]
}

// 拖拽开始处理
const onDragStart = (event, nodeType) => {
  if (event.dataTransfer) {
    event.dataTransfer.setData('application/vueflow', nodeType)
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
    height: calc(100vh - 100px); /* 根据布局调整这个值 */
    max-height: calc(100vh - 100px); /* 确保不会超出视口 */

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

.panel-title {
    font-size: 1.2rem;
    margin-bottom: 15px;
    color: var(--text);
    font-weight: 600;
    padding-bottom: 10px;
    border-bottom: 2px solid var(--border);
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

.subcategory {
    margin-left: 15px;
    margin-top: 10px;
    padding-left: 10px;
    border-left: 1px solid var(--border);
}

.subcategory-title {
    font-size: 0.95rem;
    font-weight: 500;
    color: var(--text-secondary);
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    cursor: pointer;
    padding: 6px 10px;
    border-radius: 4px;
    transition: all 0.3s ease;
    user-select: none;
}

.subcategory-title:hover {
    background: rgba(41, 98, 255, 0.05);
}

.subcategory-title i:first-child {
    margin-right: 6px;
    font-size: 0.8rem;
    color: var(--secondary);
}

.subcategory-title span {
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

.component-header {
    display: flex;
    align-items: center;
    margin-bottom: 8px;
    gap: 10px;
}

.component-icon {
    width: 24px;
    height: 24px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(41, 98, 255, 0.1);
    border-radius: 4px;
    color: var(--primary);
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

.strategy-node .component-icon {
    background: rgba(41, 98, 255, 0.1);
    color: var(--primary);
}

.feature-node .component-icon {
    background: rgba(156, 39, 176, 0.1);
    color: #9c27b0;
}

.operation-node .component-icon {
    background: rgba(255, 152, 0, 0.1);
    color: #ff9800;
}
</style>