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

                    <!-- 按钮类型 -->
                    <ButtonParam
                        v-else-if="paramConfig.type === 'button'"
                        :param-key="key"
                        :param-config="paramConfig"
                        @click="handleParamButtonClick(key)"
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
import { getNodeIcon, getNodeColor } from '@/lib/nodes'
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
import ButtonParam from './params/ButtonParam.vue'

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

const validParamTypes = ['text', 'select', 'date', 'daterange', 'multiselect', 'number', 'multiselect-dropdown', 'directory', 'file', 'config-select', 'button']

const props = defineProps<{
    node: FlowNode
}>()

const emit = defineEmits(['update-node', 'node-click', 'node-context-menu', 'open-portfolio-manager', 'visualize-debug'])

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

    // 滑点模型联动：切换模型时显隐对应参数
    if (paramKey === 'slippageModel' && props.node.data.nodeType === 'execution') {
        const useImpact = newValue === 1
        const params = props.node.data.params
        // 固定滑点 vs 冲击滑点互斥显示
        params.slippage.visible = !useImpact
        params.slippageBase.visible = useImpact
        params.slippageImpactK.visible = useImpact
        params.slippageAlpha.visible = useImpact
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

const handleParamButtonClick = (paramKey: string) => {
    if (paramKey === 'visualize') {
        emit('visualize-debug', props.node.id)
    }
}

onMounted(() => {
    // 初始化滑点参数可见性
    if (props.node.data.nodeType === 'execution') {
        const params = props.node.data.params
        const useImpact = params.slippageModel?.value === 1
        if (params.slippage) params.slippage.visible = !useImpact
        if (params.slippageBase) params.slippageBase.visible = useImpact
        if (params.slippageImpactK) params.slippageImpactK.visible = useImpact
        if (params.slippageAlpha) params.slippageAlpha.visible = useImpact
    }

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
    display: flex !important;
    align-items: center !important;
    margin: 8px 0 !important;
    position: relative !important;
    min-height: 28px !important;
}

.param-label {
    flex: 1 !important;
    font-size: 0.8rem !important;
    color: var(--text-secondary) !important;
    margin-right: 8px !important;
}

.param-control {
    flex: 2 !important;
    display: flex !important;
    align-items: center !important;
}

/* ============================================
   Flow Node Input/Select/Date 美化样式
   与 index.html 主题一致
   ============================================ */

/* 输入框+单位容器 */
.input-with-unit {
    display: flex;
    align-items: center;
    gap: 6px;
    width: 100%;
}

/* 通用输入框样式 - 强制暗色主题 */
.param-input,
input.param-input,
input.param-text,
input.param-number,
input.param-date,
select.param-input,
select.param-select,
textarea.param-input,
textarea.param-textarea {
    background: rgba(0, 0, 0, 0.3) !important;
    border: 1px solid var(--border) !important;
    border-radius: 6px !important;
    color: var(--text) !important;
    padding: 5px 10px !important;
    font-size: 0.8rem !important;
    outline: none !important;
    transition: all 0.2s ease !important;
    width: 100% !important;
    font-family: inherit !important;
    line-height: 1.4 !important;
}

/* 聚焦状态 */
.param-input:focus,
input.param-input:focus,
select.param-input:focus,
textarea.param-input:focus {
    border-color: var(--primary) !important;
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
    background: rgba(0, 0, 0, 0.4) !important;
}

/* 悬停状态 */
.param-input:hover:not(:focus),
input.param-input:hover:not(:focus),
select.param-input:hover:not(:focus) {
    border-color: rgba(41, 98, 255, 0.5) !important;
}

/* 文本输入框 */
.param-text,
input[type="text"] {
    color: var(--text) !important;
}

/* 数字输入框 */
.param-number,
input[type="number"] {
    color: var(--text) !important;
    -moz-appearance: textfield !important;
}

.param-number::-webkit-outer-spin-button,
.param-number::-webkit-inner-spin-button,
input[type="number"]::-webkit-outer-spin-button,
input[type="number"]::-webkit-inner-spin-button {
    -webkit-appearance: none !important;
    margin: 0 !important;
}

/* 日期输入框 */
.param-date,
input[type="date"],
input[type="datetime-local"] {
    color: var(--text) !important;
    color-scheme: dark !important;
}

.param-date::-webkit-calendar-picker-indicator,
input[type="date"]::-webkit-calendar-picker-indicator,
input[type="datetime-local"]::-webkit-calendar-picker-indicator {
    filter: invert(0.7) !important;
    cursor: pointer !important;
}

/* 下拉选择框 */
.param-select,
select.param-select {
    background: rgba(0, 0, 0, 0.3) !important;
    border: 1px solid var(--border) !important;
    border-radius: 6px !important;
    color: var(--text) !important;
    padding: 5px 10px !important;
    font-size: 0.8rem !important;
    outline: none !important;
    cursor: pointer !important;
    transition: all 0.2s ease !important;
    width: 100% !important;
    appearance: none !important;
    -webkit-appearance: none !important;
    background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23a0aec0' d='M6 8L1 3h10z'/%3E%3C/svg%3E") !important;
    background-repeat: no-repeat !important;
    background-position: right 8px center !important;
    padding-right: 28px !important;
}

.param-select:focus,
select.param-select:focus {
    border-color: var(--primary) !important;
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
}

.param-select:hover:not(:focus),
select.param-select:hover:not(:focus) {
    border-color: rgba(41, 98, 255, 0.5) !important;
}

.param-select option,
select option {
    background: var(--panel-bg) !important;
    color: var(--text) !important;
    padding: 8px !important;
}

/* 文本域 */
.param-textarea,
textarea.param-textarea {
    background: rgba(0, 0, 0, 0.3) !important;
    border: 1px solid var(--border) !important;
    border-radius: 6px !important;
    color: var(--text) !important;
    padding: 8px 10px !important;
    font-size: 0.8rem !important;
    outline: none !important;
    transition: all 0.2s ease !important;
    width: 100% !important;
    resize: vertical !important;
    min-height: 60px !important;
    font-family: inherit !important;
}

.param-textarea:focus,
textarea.param-textarea:focus {
    border-color: var(--primary) !important;
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
    background: rgba(0, 0, 0, 0.4) !important;
}

/* 禁用状态 */
.param-input:disabled,
input.param-input:disabled,
select.param-input:disabled {
    opacity: 0.6 !important;
    cursor: not-allowed !important;
}

/* 单位标签 */
.param-unit {
    color: var(--text-secondary) !important;
    font-size: 0.75rem !important;
    white-space: nowrap !important;
    flex-shrink: 0 !important;
}

/* 多选下拉框容器 */
.multiselect-dropdown-wrapper {
    position: relative;
    width: 100%;
}

.multiselect-dropdown {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 5px 10px;
    cursor: pointer;
    transition: all 0.2s ease;
    min-height: 30px;
}

.multiselect-dropdown:hover {
    border-color: rgba(41, 98, 255, 0.5);
}

.selected-options-display {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
    flex: 1;
    min-width: 0;
}

.selected-option-tag {
    background: rgba(41, 98, 255, 0.2);
    border: 1px solid rgba(41, 98, 255, 0.4);
    border-radius: 4px;
    padding: 1px 6px;
    font-size: 0.7rem;
    color: var(--primary);
    white-space: nowrap;
}

.placeholder {
    color: var(--text-secondary);
    font-size: 0.8rem;
}

.dropdown-arrow {
    color: var(--text-secondary);
    font-size: 0.7rem;
    transition: transform 0.2s ease;
    flex-shrink: 0;
    margin-left: 6px;
}

.dropdown-arrow.open {
    transform: rotate(180deg);
}

.dropdown-options {
    position: absolute;
    top: calc(100% + 4px);
    left: 0;
    right: 0;
    background: var(--panel-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    z-index: 1000;
    max-height: 200px;
    overflow-y: auto;
}

.dropdown-option {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 12px;
    cursor: pointer;
    transition: background 0.15s ease;
    font-size: 0.8rem;
    color: var(--text);
}

.dropdown-option:hover {
    background: rgba(41, 98, 255, 0.15);
}

.dropdown-option input[type="checkbox"] {
    accent-color: var(--primary);
    width: 14px;
    height: 14px;
    cursor: pointer;
}

.option-label {
    flex: 1;
}

/* 多选框组 */
.checkbox-group {
    display: flex;
    flex-direction: column;
    gap: 6px;
}

.checkbox-item {
    display: flex;
    align-items: center;
    gap: 8px;
    cursor: pointer;
    font-size: 0.8rem;
    color: var(--text);
}

.checkbox-item input[type="checkbox"] {
    accent-color: var(--primary);
    width: 14px;
    height: 14px;
    cursor: pointer;
}

.checkbox-item label {
    cursor: pointer;
    color: var(--text-secondary);
    transition: color 0.15s ease;
}

.checkbox-item:hover label {
    color: var(--text);
}

/* 日期范围选择器 */
.date-range-group {
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
}

.date-range-inputs {
    display: flex;
    align-items: center;
    gap: 8px;
    flex: 1;
}

.date-range-separator {
    color: var(--text-secondary);
    font-size: 0.8rem;
    flex-shrink: 0;
}

/* 配置选择器 */
.config-select-wrapper {
    display: flex;
    flex-direction: column;
    gap: 8px;
    width: 100%;
}

.config-summary-card {
    background: rgba(0, 0, 0, 0.2);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 8px 10px;
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.summary-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.75rem;
}

.summary-label {
    color: var(--text-secondary);
}

.summary-value {
    color: var(--text);
    font-weight: 500;
}

.summary-value.positive {
    color: var(--secondary);
}

.create-config-btn {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    background: rgba(41, 98, 255, 0.1);
    border: 1px dashed rgba(41, 98, 255, 0.4);
    border-radius: 6px;
    padding: 6px 12px;
    color: var(--primary);
    font-size: 0.8rem;
    cursor: pointer;
    transition: all 0.2s ease;
}

.create-config-btn:hover {
    background: rgba(41, 98, 255, 0.2);
    border-style: solid;
}

/* 参数值默认显示 */
.param-value {
    color: var(--text);
    font-size: 0.8rem;
}

/* 文本域 */
.textarea-wrapper {
    width: 100%;
}

.param-textarea {
    width: 100%;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--text);
    padding: 8px 10px;
    font-size: 0.8rem;
    outline: none;
    resize: vertical;
    min-height: 60px;
    font-family: inherit;
    transition: all 0.2s ease;
}

.param-textarea:focus {
    border-color: var(--primary);
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
}

.param-textarea::placeholder {
    color: var(--text-secondary);
    opacity: 0.6;
}

/* 目录选择器 */
.directory-input,
.file-input {
    width: 100%;
}

.directory-path-display,
.file-path-display {
    display: flex;
    align-items: center;
    gap: 8px;
    background: rgba(0, 0, 0, 0.3);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 5px 10px;
    transition: all 0.2s ease;
}

.directory-path-display:hover,
.file-path-display:hover {
    border-color: rgba(41, 98, 255, 0.5);
}

.path-text {
    flex: 1;
    color: var(--text-secondary);
    font-size: 0.8rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.directory-select-btn,
.file-select-btn {
    display: flex;
    align-items: center;
    gap: 4px;
    background: rgba(41, 98, 255, 0.1);
    border: 1px solid rgba(41, 98, 255, 0.3);
    border-radius: 4px;
    color: var(--primary);
    padding: 4px 10px;
    font-size: 0.75rem;
    cursor: pointer;
    transition: all 0.2s ease;
    white-space: nowrap;
}

.directory-select-btn:hover,
.file-select-btn:hover {
    background: rgba(41, 98, 255, 0.2);
    border-color: var(--primary);
}

.directory-select-btn i,
.file-select-btn i {
    font-size: 0.8rem;
}

/* 下载按钮 */
.download-section {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
}

.download-btn {
    display: flex;
    align-items: center;
    gap: 6px;
    background: linear-gradient(135deg, var(--primary), #1d4ed8);
    border: none;
    border-radius: 6px;
    color: white;
    padding: 6px 14px;
    font-size: 0.8rem;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.2s ease;
}

.download-btn:hover:not(:disabled) {
    transform: translateY(-1px);
    box-shadow: 0 4px 12px rgba(41, 98, 255, 0.3);
}

.download-btn:active:not(:disabled) {
    transform: translateY(0);
}

.download-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.download-btn i {
    font-size: 0.85rem;
}

.download-status {
    color: var(--text-secondary);
    font-size: 0.75rem;
}

/* 连接线样式 */
.connection-handle {
    width: 12px !important;
    height: 12px !important;
    border-radius: 50% !important;
    border: 2px solid var(--panel-bg) !important;
    transition: all 0.2s ease !important;
}

.left-handle {
    background: var(--secondary) !important;
    left: -6px !important;
}

.right-handle {
    background: var(--primary) !important;
    right: -6px !important;
}

.connection-handle:hover {
    transform: scale(1.2);
    box-shadow: 0 0 8px rgba(41, 98, 255, 0.5);
}

.node-connection-row {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    pointer-events: none;
}

.left-handle,
.right-handle {
    pointer-events: auto;
}

/* ============================================
   全局样式 - 非 scoped，用于原生 select option 元素
   ============================================ */

/* 确保所有 select 的 option 使用暗色主题 */
.vue-flow__node-custom select option,
.node-content select option {
    background: var(--panel-bg) !important;
    color: var(--text) !important;
}

/* 原生 select 元素全局样式 */
.node-content select,
.node-param select,
.vue-flow__node-custom select {
    background: rgba(0, 0, 0, 0.3) !important;
    border: 1px solid var(--border) !important;
    border-radius: 6px !important;
    color: var(--text) !important;
    padding: 5px 10px !important;
    font-size: 0.8rem !important;
    outline: none !important;
    cursor: pointer !important;
    width: 100% !important;
    appearance: none !important;
    -webkit-appearance: none !important;
    background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23a0aec0' d='M6 8L1 3h10z'/%3E%3C/svg%3E") !important;
    background-repeat: no-repeat !important;
    background-position: right 8px center !important;
    padding-right: 28px !important;
}

.node-content select:focus,
.node-param select:focus,
.vue-flow__node-custom select:focus {
    border-color: var(--primary) !important;
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
    background: rgba(0, 0, 0, 0.4) !important;
}

.node-content select:hover:not(:focus),
.node-param select:hover:not(:focus),
.vue-flow__node-custom select:hover:not(:focus) {
    border-color: rgba(41, 98, 255, 0.5) !important;
}

/* 确保所有 input 元素使用暗色主题 */
.node-content input[type="text"],
.node-content input[type="number"],
.node-content input[type="date"],
.node-param input[type="text"],
.node-param input[type="number"],
.node-param input[type="date"],
.vue-flow__node-custom input[type="text"],
.vue-flow__node-custom input[type="number"],
.vue-flow__node-custom input[type="date"] {
    background: rgba(0, 0, 0, 0.3) !important;
    border: 1px solid var(--border) !important;
    border-radius: 6px !important;
    color: var(--text) !important;
    padding: 5px 10px !important;
    font-size: 0.8rem !important;
    outline: none !important;
    transition: all 0.2s ease !important;
    width: 100% !important;
}

.node-content input[type="text"]:focus,
.node-content input[type="number"]:focus,
.node-content input[type="date"]:focus,
.node-param input[type="text"]:focus,
.node-param input[type="number"]:focus,
.node-param input[type="date"]:focus,
.vue-flow__node-custom input[type="text"]:focus,
.vue-flow__node-custom input[type="number"]:focus,
.vue-flow__node-custom input[type="date"]:focus {
    border-color: var(--primary) !important;
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
    background: rgba(0, 0, 0, 0.4) !important;
}

.node-content input[type="text"]:hover:not(:focus),
.node-content input[type="number"]:hover:not(:focus),
.node-content input[type="date"]:hover:not(:focus),
.node-param input[type="text"]:hover:not(:focus),
.node-param input[type="number"]:hover:not(:focus),
.node-param input[type="date"]:hover:not(:focus),
.vue-flow__node-custom input[type="text"]:hover:not(:focus),
.vue-flow__node-custom input[type="number"]:hover:not(:focus),
.vue-flow__node-custom input[type="date"]:hover:not(:focus) {
    border-color: rgba(41, 98, 255, 0.5) !important;
}

/* 日期选择器特殊处理 */
.node-content input[type="date"],
.node-param input[type="date"],
.vue-flow__node-custom input[type="date"] {
    color-scheme: dark !important;
}

.node-content input[type="date"]::-webkit-calendar-picker-indicator,
.node-param input[type="date"]::-webkit-calendar-picker-indicator,
.vue-flow__node-custom input[type="date"]::-webkit-calendar-picker-indicator {
    filter: invert(0.7) !important;
    cursor: pointer !important;
}

/* 数字输入框保留默认箭头，但调整样式适配暗色主题 */
.node-content input[type="number"],
.node-param input[type="number"],
.vue-flow__node-custom input[type="number"] {
    -moz-appearance: textfield !important;
}

/* Webkit 浏览器（Chrome/Edge/Safari）的自定义箭头样式 - 暗色主题 */
.node-content input[type="number"]::-webkit-outer-spin-button,
.node-content input[type="number"]::-webkit-inner-spin-button,
.node-param input[type="number"]::-webkit-outer-spin-button,
.node-param input[type="number"]::-webkit-inner-spin-button,
.vue-flow__node-custom input[type="number"]::-webkit-outer-spin-button,
.vue-flow__node-custom input[type="number"]::-webkit-inner-spin-button {
    -webkit-appearance: inner-spin-button !important;
    appearance: inner-spin-button !important;
    margin: 0 !important;
    background: rgba(0, 0, 0, 0.2) !important;
    border-left: 1px solid var(--border) !important;
    opacity: 0.7 !important;
}

.node-content input[type="number"]:hover::-webkit-outer-spin-button,
.node-content input[type="number"]:hover::-webkit-inner-spin-button,
.node-param input[type="number"]:hover::-webkit-outer-spin-button,
.node-param input[type="number"]:hover::-webkit-inner-spin-button,
.vue-flow__node-custom input[type="number"]:hover::-webkit-outer-spin-button,
.vue-flow__node-custom input[type="number"]:hover::-webkit-inner-spin-button {
    opacity: 1 !important;
}
</style>
