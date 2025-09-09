<template>
    <div>
        <div class="flow-container">
            <VueFlow :nodes="nodes" :edges="edges">
                <template #node-custom="{ data }">
                    <div class="vue-flow__node-custom">
                        <div class="node-header">
                            <div class="node-icon">
                                <i class="fas fa-cube"></i>
                            </div>
                            <div class="node-title">{{ data.label }}</div>
                        </div>
                        <div class="node-content">
                            <div class="node-param" v-for="(value, key) in data.params" :key="key">
                                {{ key }}: {{ value }}
                            </div>
                        </div>
                    </div>
                </template>
            </VueFlow>
        </div>
        
    </div>
</template>
<script setup>
import { ref } from 'vue'
import { useVueFlow, VueFlow } from '@vue-flow/core'

const { onInit, findNode, addEdges, onConnect, onNodeDragStop, setViewport, fitView, snapToGrid } = useVueFlow()

let initial_nodes = [
  {
    id: '1',
    type: 'custom',
    data: { 
        label: '数据输入',
        params: {
            来源: 'CSV文件',
            路径: '/data/input.csv'
        }
    },
    position: { x: 100, y: 0 },
    class: 'light',
},
{
    id: '2',
    type: 'custom',
    data: { 
        label: '数据预处理',
        params: {
            方法: 'Z-score',
            缺失值: '填充'
        }
    },
    position: { x: 0, y: 100 },
    class: 'light',
    },
    {
    id: '3',
    type: 'custom',
    data: { 
        label: '特征工程',
        params: {
            特征选择: '是',
            降维: 'PCA'
        }
    },
    position: { x: 200, y: 100 },
    class: 'light',
},
{
    id: '4',
    type: 'custom',
    data: { 
        label: '模型训练',
        params: {
            算法: 'XGBoost',
            objective: 'binary:logistic',
            eval_metric: 'logloss',
            max_depth: 3,
            eta: 0.0001,
            迭代次数: 100
        }
    },
    position: { x: 100, y: 200 },
    class: 'light',
},
{
    id: '6',
    type: 'custom',
    data: { 
        label: '模型训练',
        params: {
            算法: 'LSTM',
            objective: 'binary:logistic',
            input: 5,
            迭代次数: 100
        }
    },
    position: { x: 100, y: 200 },
    class: 'light',
},
{
    id: '5',
    type: 'custom',
    data: { 
        label: '结果输出',
        params: {
            格式: 'CSV',
            路径: '/data/output.csv'
        }
    },
    position: { x: 100, y: 300 },
    class: 'light',
    },
]

let initial_edges = [
    {
        id: 'e1->2',
        source: '1',
        target: '2',
        // animated: true,
        // label: '原始数据'
    },
    {
        id: 'e2->3',
        source: '2',
        target: '3',
        // label: '数据流',
        // type: 'smoothstep'
    },
    {
        id: 'e3->4',
        source: '3',
        target: '4',
        // label: '预处理数据',
        // type: 'step',
        // style: { stroke: '#4f46e5' }
    },
    {
        id: 'e3->4',
        source: '3',
        target: '4',
        // label: '特征数据',
        // type: 'smoothstep',
        // style: { stroke: '#4f46e5' }
    },
    {
        id: 'e3->6',
        source: '3',
        target: '6',
        // label: '特征数据',
        // type: 'smoothstep',
        // style: { stroke: '#4f46e5' }
    },
    {
        id: 'e6->5',
        source: '6',
        target: '5',
        // label: '特征数据',
        // type: 'smoothstep',
        // style: { stroke: '#4f46e5' }
    },
    {
        id: 'e4->5',
        source: '4',
        target: '5',
        // label: '预测结果',
        // type: 'smoothstep',
        // style: { stroke: '#4f46e5' }
    },
]

let nodes = ref(initial_nodes)
let edges = ref(initial_edges)

// onInit((vueFlowInstance) => {
  // instance is the same as the return of `useVueFlow`
//   vueFlowInstance.fitView()
// })

// onConnect((params) => {
//     addEdges(params)
//   addEdges([{...params, type: 'smoothstep', animated: true, label: '数据流'}]);
// })

</script>
<style scoped>

.flow-container {
    flex: 1;
    position: relative;
    margin-top: 110px;
    margin-right: 280px;
    height: calc(100vh - 110px);
}
/* 自定义节点样式 */
.vue-flow__node-custom {
    padding: 10px;
    border-radius: 8px;
    border: 2px solid #4f46e5;
    background: white;
    box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);
    min-width: 150px;
    font-size: 0.9rem;
}

.vue-flow__node-custom.selected {
    border-color: #6a11cb;
    box-shadow: 0 2px 10px rgba(106, 17, 203, 0.2);
}

.node-header {
    display: flex;
    align-items: center;
    margin-bottom: 8px;
    padding-bottom: 8px;
    border-bottom: 1px solid #eee;
}

.node-icon {
    width: 24px;
    height: 24px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 10px;
    color: white;
    font-size: 12px;
    background-color: #4f46e5;
}

.node-title {
    font-weight: 600;
    color: #2c3e50;
}

.node-content {
    padding: 5px 0;
}

.node-param {
    font-size: 0.8rem;
    margin: 3px 0;
    color: #555;
}

.vue-flow__edge-path {
    stroke: #b1b1b7;
    stroke-width: 2;
}

.vue-flow__edge.selected .vue-flow__edge-path {
    stroke: #6a11cb;
    stroke-width: 3;
}

.vue-flow__edge.animated path {
    stroke-dasharray: 5;
    animation: dashdraw 0.5s linear infinite;
}

@keyframes dashdraw {
    from { stroke-dashoffset: 10; }
}

.vue-flow__connectionline {
    z-index: 1000;
}
</style>