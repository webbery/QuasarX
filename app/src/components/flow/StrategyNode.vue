<!-- src/components/flow/StrategyNode.vue -->
<!-- 策略节点主组件 -->

<template>
    <div class="vue-flow__node-custom" :class="[nodeClass, { 'selected': isSelected, 'multi-selected': isMultiSelected }]"
        @click.stop="onNodeClick"
        @contextmenu="onNodeRightClick"
    >
        <!-- 节点头部 -->
        <StrategyNodeHeader
            :label="node.data.label"
            :icon="iconType"
            :header-class="headerClass"
            :icon-class="iconClass"
            :is-editing="isEditing"
            :editing-label="editingLabel"
            :is-multi-selected="isMultiSelected"
            :selection-index="selectionIndex"
            @start-editing="startEditing"
            @save-editing="handleSaveEditing"
            @cancel-editing="cancelEditing"
            @input-keydown="onTitleInputKeydown"
        />

        <!-- 连接点 -->
        <StrategyNodeHandles :node-type="nodeType" />

        <!-- 参数列表 -->
        <div class="node-content">
            <div
                class="node-param"
                v-for="(paramConfig, key) in node.data.params"
                :key="key"
                v-show="paramConfig.visible !== false"
                :class="{ 'data-field-param': isDataFieldParam(key) }"
            >
                <div class="param-label" v-if="validParamTypes.includes(paramConfig.type)">{{ key }}</div>
                <div class="param-control">
                    <!-- 配置选择器 -->
                    <ConfigSelectParam
                        v-if="paramConfig.type === 'config-select'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        :config-options="configOptions"
                        @update="updateParam(key, $event)"
                        @open-config-manager="openConfigManager"
                    />

                    <!-- 下拉选择框 -->
                    <SelectParam
                        v-else-if="paramConfig.type === 'select'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @update="updateParam(key, $event)"
                    />

                    <!-- 多选下拉框 -->
                    <MultiSelectDropdownParam
                        v-else-if="paramConfig.type === 'multiselect-dropdown'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        :is-open="isDropdownOpen(key)"
                        @toggle-dropdown="toggleDropdown"
                        @toggle-option="toggleOptionSelection(key, $event)"
                    />

                    <!-- 文本输入框 -->
                    <TextParam
                        v-else-if="paramConfig.type === 'text'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @update="updateParam(key, $event)"
                        @keydown="onTitleInputKeydown"
                    />

                    <!-- 多行文本域 -->
                    <TextareaParam
                        v-else-if="paramConfig.type === 'textarea'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @update="updateParam(key, $event)"
                        @keydown="onTitleInputKeydown"
                    />

                    <!-- 数字输入框 -->
                    <NumberParam
                        v-else-if="paramConfig.type === 'number'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @update="updateParam(key, $event)"
                    />

                    <!-- 日期选择器 -->
                    <DateParam
                        v-else-if="paramConfig.type === 'date'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @update="updateParam(key, $event)"
                    />

                    <!-- 日期范围选择器 -->
                    <DateRangeParam
                        v-else-if="paramConfig.type === 'daterange'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @update="updateDateRange(key, $event)"
                    />

                    <!-- 多选框 -->
                    <MultiSelectParam
                        v-else-if="paramConfig.type === 'multiselect'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        :node-id="node.id"
                        @update="updateParam(key, $event)"
                    />

                    <!-- 目录选择 -->
                    <DirectoryParam
                        v-else-if="paramConfig.type === 'directory'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @select-directory="selectDirectory"
                    />

                    <!-- 文件选择 -->
                    <FileParam
                        v-else-if="paramConfig.type === 'file'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        @select-file="selectFile"
                    />

                    <!-- 下载按钮 -->
                    <DownloadParam
                        v-else-if="paramConfig.type === 'download'"
                        :param-key="key"
                        :param-config="paramConfig"
                        :value="paramConfig.value"
                        :can-download="canDownload"
                        :download-status="downloadStatus"
                        @download="downloadFile"
                    />

                    <!-- 默认显示 -->
                    <span v-else class="param-value">
                        {{ paramConfig.value }}
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                    </span>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup lang="ts">
import { computed, inject, ref, onMounted, onUnmounted, type PropType } from 'vue'
import { getNodeIcon, getNodeColor } from './config/nodeTypeConfig'
import { useNodeSelection } from './composables/useNodeSelection'
import { useNodeEditing } from './composables/useNodeEditing'
import { useNodeDropdown } from './composables/useNodeDropdown'
import { useNodeDownload } from './composables/useNodeDownload'

// 子组件导入
import StrategyNodeHeader from './StrategyNodeHeader.vue'
import StrategyNodeHandles from './StrategyNodeHandles.vue'
import TextParam from './params/TextParam.vue'
import NumberParam from './params/NumberParam.vue'
import SelectParam from './params/SelectParam.vue'
import MultiSelectParam from './params/MultiSelectParam.vue'
import MultiSelectDropdownParam from './params/MultiSelectDropdownParam.vue'
import DateParam from './params/DateParam.vue'
import DateRangeParam from './params/DateRangeParam.vue'
import TextareaParam from './params/TextareaParam.vue'
import DirectoryParam from './params/DirectoryParam.vue'
import FileParam from './params/FileParam.vue'
import ConfigSelectParam from './params/ConfigSelectParam.vue'
import DownloadParam from './params/DownloadParam.vue'

interface FlowNodeData {
  id: string
  label: string
  nodeType: string
  params: Record<string, any>
  [key: string]: any
}

interface FlowNode {
  id: string
  data: FlowNodeData
  [key: string]: any
}

const validParamTypes = ['text', 'select', 'date', 'daterange', 'multiselect', 'number', 'multiselect-dropdown', 'directory', 'file', 'config-select']

const props = defineProps<{
    node: FlowNode
}>()

const emit = defineEmits(['update-node', 'node-click', 'node-context-menu', 'open-portfolio-manager'])

const selectedNodes = inject('selectedNodes', [])
const portfolioConfigs = inject('portfolioConfigs', ref([]))

// 使用 composables
const { isSelected, isMultiSelected, selectionIndex } = useNodeSelection(
    props.node.id,
    selectedNodes as any
)

const { isEditing, editingLabel, startEditing, saveEditing, cancelEditing, handleKeydown } = useNodeEditing(
    props.node.data.label
)

const { toggleDropdown, closeAllDropdowns, isDropdownOpen } = useNodeDropdown()
const { downloadStatus, canDownload, downloadFile } = useNodeDownload(props.node.data)

// 计算属性
const nodeType = computed(() => props.node.data.nodeType)
const nodeClass = computed(() => `node-type-${nodeType.value}`)
const headerClass = computed(() => `header-type-${nodeType.value}`)
const iconClass = computed(() => `icon-type-${nodeType.value}`)
const iconType = computed(() => getNodeIcon(props.node.data.label))

const configOptions = computed(() => {
    const configs = (portfolioConfigs as any).value || []
    return configs.map((config: any) => ({
        id: config.id,
        name: config.name,
        modelType: config.modelType,
        modelTypeText: getModelTypeText(config.modelType),
        securities: config.securities,
        views: config.views,
        optimizationResult: config.optimizationResult
    }))
})

const getModelTypeText = (modelType: string) => {
    const map: Record<string, string> = {
        'black_litterman': 'Black-Litterman',
        'mean_variance': 'Mean-Variance',
        'risk_parity': 'Risk Parity'
    }
    return map[modelType] || modelType || '-'
}

const isDataFieldParam = (key: string) => {
    return ['close', 'open', 'high', 'low', 'volume'].includes(key)
}

// 事件处理
const onNodeClick = (event: MouseEvent) => {
    if (isEditing.value) return
    closeAllDropdowns()
    emit('node-click', { node: props.node, event })
}

const onNodeRightClick = (event: MouseEvent) => {
    event.preventDefault()
    closeAllDropdowns()
    emit('node-context-menu', { node: props.node, event })
}

const handleSaveEditing = () => {
    const label = saveEditing()
    if (label !== null) {
        emit('update-node', props.node.id, 'label', label)
        props.node.data.label = label
    }
}

const onTitleInputKeydown = (event: KeyboardEvent) => {
    handleKeydown(event)
}

const updateParam = (paramKey: string, newValue: any) => {
    if (props.node.data.nodeType === 'portfolio' && paramKey === '预设模板') {
        if (newValue === 'full_position') {
            emit('update-node', props.node.id, '仓位比例', 1.0)
        } else if (newValue === 'empty_position') {
            emit('update-node', props.node.id, '仓位比例', 0.0)
        }
    }
    emit('update-node', props.node.id, paramKey, newValue)
}

const updateDateRange = (paramKey: string, newValue: any) => {
    updateParam(paramKey, newValue)
}

const toggleOptionSelection = (paramKey: string, option: string) => {
    const currentValue = props.node.data.params[paramKey]?.value || []
    let newValue

    if (currentValue.includes(option)) {
        newValue = currentValue.filter((item: string) => item !== option)
    } else {
        newValue = [...currentValue, option]
    }

    updateParam(paramKey, newValue)
}

const openConfigManager = () => {
    emit('open-portfolio-manager')
    window.dispatchEvent(new CustomEvent('open-portfolio-manager'))
}

const selectDirectory = async (paramKey: string) => {
    // TODO: 实现目录选择
}

const selectFile = async (paramKey: string) => {
    // TODO: 实现文件选择
}

onMounted(() => {
    document.addEventListener('click', handleClickOutside)
})

onUnmounted(() => {
    document.removeEventListener('click', handleClickOutside)
})

const handleClickOutside = (event: MouseEvent) => {
    if (isEditing.value && !(event.target as HTMLElement).closest('.node-title')) {
        handleSaveEditing()
    }
}
</script>

<style scoped>
/* 保持原有样式不变 */
.vue-flow__node-custom {
    padding: 10px;
    border-radius: 8px;
    background: var(--panel-bg);
    box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
    min-width: 180px;
    font-size: 0.9rem;
    color: var(--text);
    border: 2px solid;
    display: flex;
    flex-direction: column;
    position: relative;
    cursor: grab;
}

.vue-flow__node-custom:active {
    cursor: grabbing;
}

.vue-flow__node-custom:hover {
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
    transform: translateY(-1px);
    transition: all 0.2s ease;
}

.node-content {
    padding: 5px 0;
    flex: 1;
}

.node-param {
    display: flex;
    align-items: center;
    margin: 8px 0;
    position: relative;
    min-height: 28px;
}

.param-label {
    flex: 1;
    font-size: 0.8rem;
    color: var(--text-secondary);
    margin-right: 8px;
}

.param-control {
    flex: 2;
    display: flex;
    align-items: center;
}

/* 其余样式从原 FlowNode.vue 复制 */
</style>
