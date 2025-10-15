<template>
<div class="components-panel">
    <div class="panel-title">机器学习组件</div>
    
    <!-- 输入节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('input')">
            <i class="fas fa-download"></i>
            <span>输入节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.input }"></i>
        </div>
        <div class="components-list" v-show="openCategories.input">
            <div class="component-card input-node" draggable="true" @dragstart="onDragStart($event, 'csv-data-source')">
                <div class="component-header">
                    <div class="component-icon">
                        <i class="fas fa-file-csv"></i>
                    </div>
                    <div class="component-title">CSV数据源</div>
                </div>
                <div class="component-desc">从CSV文件加载数据</div>
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
            <div class="component-card output-node" draggable="true">
                <div class="component-header">
                    <div class="component-icon">
                        <i class="fas fa-chart-bar"></i>
                    </div>
                    <div class="component-title">结果可视化</div>
                </div>
                <div class="component-desc">将结果以图表形式展示</div>
            </div>
            
            <div class="component-card output-node" draggable="true">
                <div class="component-header">
                    <div class="component-icon">
                        <i class="fas fa-file-export"></i>
                    </div>
                    <div class="component-title">模型导出</div>
                </div>
                <div class="component-desc">将训练好的模型保存到文件</div>
            </div>
        </div>
    </div>
    
    <!-- 策略节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('strategy')">
            <i class="fas fa-chess-knight"></i>
            <span>策略节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.strategy }"></i>
        </div>
        <div v-show="openCategories.strategy">
            <!-- 神经网络模型节点 -->
            <div class="subcategory">
                <div class="subcategory-title" @click="toggleSubcategory('neural')">
                    <i class="fas fa-microchip"></i>
                    <span>神经网络模型节点</span>
                    <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openSubcategories.neural }"></i>
                </div>
                <div class="components-list" v-show="openSubcategories.neural">
                    <div class="component-card strategy-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-brain"></i>
                            </div>
                            <div class="component-title">CNN模型</div>
                        </div>
                        <div class="component-desc">卷积神经网络，适用于图像处理</div>
                    </div>
                    
                    <div class="component-card strategy-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-network-wired"></i>
                            </div>
                            <div class="component-title">RNN模型</div>
                        </div>
                        <div class="component-desc">循环神经网络，适用于序列数据</div>
                    </div>
                </div>
            </div>
            
            <!-- 普通机器学习节点 -->
            <div class="subcategory">
                <div class="subcategory-title" @click="toggleSubcategory('ml')">
                    <i class="fas fa-cogs"></i>
                    <span>普通机器学习节点</span>
                    <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openSubcategories.ml }"></i>
                </div>
                <div class="components-list" v-show="openSubcategories.ml">
                    <div class="component-card strategy-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-chart-line"></i>
                            </div>
                            <div class="component-title">线性回归</div>
                        </div>
                        <div class="component-desc">用于预测连续值的线性模型</div>
                    </div>
                    
                    <div class="component-card strategy-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-project-diagram"></i>
                            </div>
                            <div class="component-title">决策树</div>
                        </div>
                        <div class="component-desc">基于树结构的分类与回归模型</div>
                    </div>
                </div>
            </div>
            
            <!-- 组合特征节点 -->
            <div class="subcategory">
                <div class="subcategory-title" @click="toggleSubcategory('feature')">
                    <i class="fas fa-layer-group"></i>
                    <span>组合特征节点</span>
                    <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openSubcategories.feature }"></i>
                </div>
                <div class="components-list" v-show="openSubcategories.feature">
                    <div class="component-card strategy-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-plus-circle"></i>
                            </div>
                            <div class="component-title">特征交叉</div>
                        </div>
                        <div class="component-desc">将多个特征组合生成新特征</div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <!-- 特征节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('featureNode')">
            <i class="fas fa-filter"></i>
            <span>特征节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.featureNode }"></i>
        </div>
        <div class="components-list" v-show="openCategories.featureNode">
            <div class="component-card feature-node" draggable="true">
                <div class="component-header">
                    <div class="component-icon">
                        <i class="fas fa-sliders-h"></i>
                    </div>
                    <div class="component-title">特征选择</div>
                </div>
                <div class="component-desc">选择最相关的特征子集</div>
            </div>
            
            <div class="component-card feature-node" draggable="true">
                <div class="component-header">
                    <div class="component-icon">
                        <i class="fas fa-compress-arrows-alt"></i>
                    </div>
                    <div class="component-title">特征提取</div>
                </div>
                <div class="component-desc">从原始特征中提取新特征</div>
            </div>
        </div>
    </div>
    
    <!-- 运算节点 -->
    <div class="category">
        <div class="category-title" @click="toggleCategory('operation')">
            <i class="fas fa-calculator"></i>
            <span>运算节点</span>
            <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openCategories.operation }"></i>
        </div>
        <div v-show="openCategories.operation">
            <!-- 合并节点 -->
            <div class="subcategory">
                <div class="subcategory-title" @click="toggleSubcategory('merge')">
                    <i class="fas fa-object-group"></i>
                    <span>合并节点</span>
                    <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openSubcategories.merge }"></i>
                </div>
                <div class="components-list" v-show="openSubcategories.merge">
                    <div class="component-card operation-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-link"></i>
                            </div>
                            <div class="component-title">数据连接</div>
                        </div>
                        <div class="component-desc">基于键值连接多个数据集</div>
                    </div>
                </div>
            </div>
            
            <!-- 归一化节点 -->
            <div class="subcategory">
                <div class="subcategory-title" @click="toggleSubcategory('normalization')">
                    <i class="fas fa-balance-scale"></i>
                    <span>归一化节点</span>
                    <i class="fas fa-chevron-down arrow" :class="{ 'rotate-180': !openSubcategories.normalization }"></i>
                </div>
                <div class="components-list" v-show="openSubcategories.normalization">
                    <div class="component-card operation-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-ruler-combined"></i>
                            </div>
                            <div class="component-title">最小-最大归一化</div>
                        </div>
                        <div class="component-desc">将数据缩放到指定范围</div>
                    </div>
                    
                    <div class="component-card operation-node" draggable="true">
                        <div class="component-header">
                            <div class="component-icon">
                                <i class="fas fa-ruler"></i>
                            </div>
                            <div class="component-title">标准化</div>
                        </div>
                        <div class="component-desc">将数据转换为均值为0，标准差为1</div>
                    </div>
                </div>
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
    width: 280px;
    background-color: var(--panel-bg);
    border-left: 1px solid var(--border);
    padding: 15px;
    overflow-y: auto;
    box-shadow: -2px 0 5px rgba(0, 0, 0, 0.2);
    margin-top: 60px;
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
    padding: 12px;
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

.component-desc {
    font-size: 0.85rem;
    color: var(--text-secondary);
    line-height: 1.4;
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