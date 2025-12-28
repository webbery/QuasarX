<template>
    <div class="vue-flow__node-custom" :class="[nodeClass, { 'selected': node.selected, 'multi-selected': isMultiSelected }]"
        @click.stop="onNodeClick"
        @contextmenu="onNodeRightClick"
    >
        
        <div class="node-header" :class="headerClass">
            <div class="node-icon" :class="iconClass">
                <i :class="iconType"></i>
            </div>
            <div class="node-title" @dblclick="startEditing">
                <span v-if="!isEditing">{{ node.data.label }}</span>
                <input
                    v-else
                    ref="titleInput"
                    v-model="editingLabel"
                    @blur="saveEditing"
                    @keyup.enter="saveEditing"
                    @keyup.esc="cancelEditing"
                    @keydown="onTitleInputKeydown"
                    class="title-input"
                    type="text"
                    @mousedown.stop
                    @dragstart.stop
                />
            </div>
            <div v-if="isMultiSelected" class="selection-badge">
                {{ selectionIndex }}
            </div>
        </div>
        
        <div class="node-content">
            <!-- 左右两侧连接点 -->
            <div class="node-connection-row">
                <!-- 左侧输入连接点 -->
                <Handle
                    v-if="nodeType !== 'input'"
                    type="target"
                    :position="Position.Left"
                    id="input"
                    class="connection-handle left-handle input-handle"
                />
                
                <!-- 右侧输出连接点 -->
                <Handle
                    v-if="nodeType !== 'output' && nodeType !== 'input' && nodeType !== 'trade'"
                    type="source"
                    :position="Position.Right"
                    id="output"
                    class="connection-handle right-handle output-handle"
                />
            </div>
            
            <div 
                class="node-param" 
                v-for="(paramConfig, key) in node.data.params" 
                :key="key"
                v-show="paramConfig.visible !== false"
                :class="{ 'data-field-param': isDataFieldParam(key) }"
            >
                <div class="param-label" v-if="validParamTypes.includes(paramConfig.type)">{{ key }}</div>
                
                <!-- 参数输入控件 -->
                <div class="param-control">
                    <!-- 下拉选择框 -->
                    <div v-if="paramConfig.type === 'select'" class="input-with-unit">
                        <select 
                            :value="paramConfig.value"
                            @change="updateParam(key, $event.target.value)"
                            class="param-input param-select"
                        >
                            <option 
                                v-for="option in paramConfig.options" 
                                :key="option"
                                :value="option"
                            >
                                {{ option }}
                            </option>
                        </select>
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                    </div>
                    
                    <!-- 多选下拉框 -->
                    <div v-else-if="paramConfig.type === 'multiselect-dropdown'" class="multiselect-dropdown-wrapper">
                        <div class="multiselect-dropdown" @click="toggleDropdown(key)">
                            <div class="selected-options-display">
                                <template v-if="getSelectedOptions(paramConfig.value, paramConfig.options).length > 0">
                                    <span class="selected-option-tag" 
                                          v-for="selectedOption in getSelectedOptions(paramConfig.value, paramConfig.options)" 
                                          :key="selectedOption">
                                        {{ selectedOption }}
                                    </span>
                                </template>
                                <span v-else class="placeholder">请选择...</span>
                            </div>
                            <div class="dropdown-arrow" :class="{ 'open': openDropdowns[key] }">
                                <i class="fas fa-chevron-down"></i>
                            </div>
                        </div>
                        
                        <div v-if="openDropdowns[key]" class="dropdown-options" @click.stop>
                            <div class="dropdown-option" 
                                 v-for="option in paramConfig.options" 
                                 :key="option"
                                 @click="toggleOptionSelection(key, option)">
                                <input type="checkbox" 
                                       :checked="isOptionSelected(paramConfig.value, option)"
                                       @change.stop />
                                <span class="option-label">{{ option }}</span>
                            </div>
                        </div>
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                    </div>

                    <!-- 文本输入框 -->
                    <div v-else-if="paramConfig.type === 'text'" class="input-with-unit">
                        <input 
                            type="text"
                            :value="paramConfig.value"
                            @input="updateParam(key, $event.target.value)"
                            @keydown="onTitleInputKeydown"
                            @mousedown.stop
                            @dragstart.stop
                            class="param-input param-text"
                        />
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                        <!-- 数据字段的输出连接点 -->
                        <Handle
                            v-if="isDataFieldParam(key)"
                            type="source"
                            :position="Position.Right"
                            :id="`field-${key}`"
                            class="field-output-handle"
                        />
                    </div>
                    
                    <!-- 数字输入框 -->
                    <div v-else-if="paramConfig.type === 'number'" class="input-with-unit">
                        <input 
                            type="number"
                            :value="paramConfig.value"
                            @input="updateParam(key, $event.target.value)"
                            :min="paramConfig.min"
                            :max="paramConfig.max"
                            :step="paramConfig.step || 1"
                            @mousedown.stop
                            @dragstart.stop
                            class="param-input param-number"
                        />
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                    </div>

                    <!-- 日期选择器 -->
                    <div v-else-if="paramConfig.type === 'date'" class="input-with-unit">
                        <input 
                            type="date"
                            :value="paramConfig.value"
                            @input="updateParam(key, $event.target.value)"
                            @mousedown.stop
                            @dragstart.stop
                            class="param-input param-date"
                        />
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                    </div>
                    
                    <!-- 日期范围选择器 -->
                    <div v-else-if="paramConfig.type === 'daterange'" class="date-range-group">
                        <div class="date-range-inputs">
                            <input 
                                type="date"
                                :value="paramConfig.value[0]"
                                @input="updateDateRange(key, 0, $event.target.value)"
                                @mousedown.stop
                                @dragstart.stop
                                class="param-input param-date"
                                title="开始日期"
                            />
                            <span class="date-range-separator">至</span>
                            <input 
                                type="date"
                                :value="paramConfig.value[1]"
                                @input="updateDateRange(key, 1, $event.target.value)"
                                @mousedown.stop
                                @dragstart.stop
                                class="param-input param-date"
                                title="结束日期"
                            />
                        </div>
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                    </div>

                    <!-- 多选框 -->
                    <div v-else-if="paramConfig.type === 'multiselect'" class="checkbox-group">
                        <div 
                            v-for="option in paramConfig.options" 
                            :key="option" 
                            class="checkbox-item"
                        >
                            <input
                                type="checkbox"
                                :id="`${node.id}-${key}-${option}`"
                                :value="option"
                                :checked="isOptionChecked(paramConfig.value, option)"
                                @change="onCheckboxChange(key, option, $event.target.checked)"
                                @mousedown.stop
                                @dragstart.stop
                            />
                            <label :for="`${node.id}-${key}-${option}`"
                                @mousedown.stop
                                @dragstart.stop
                            >{{ option }}</label>
                        </div>
                    </div>
                    <div v-else-if="paramConfig.type === 'directory'" class="directory-input">
                        <div class="directory-path-display">
                            <span class="path-text">{{ paramConfig.value || '未选择路径' }}</span>
                            <button 
                                class="directory-select-btn"
                                @click="selectDirectory(key)"
                                @mousedown.stop
                                @dragstart.stop
                            >
                                <i class="fas fa-folder-open"></i>
                                选择路径
                            </button>
                        </div>
                    </div>
                    <div v-else-if="paramConfig.type === 'file'" class="file-input">
                        <div class="file-path-display">
                            <span class="path-text">{{ paramConfig.value || '未选择路径' }}</span>
                            <button 
                                class="file-select-btn"
                                @click="selectFile(key)"
                                @mousedown.stop
                                @dragstart.stop
                            >
                                <i class="fas fa-file"></i>
                                选择文件
                            </button>
                        </div>
                    </div>
                    <!-- 下载按钮 -->
                    <div v-else-if="paramConfig.type === 'download'" class="download-section">
                        <button 
                            class="download-btn"
                            @click="downloadFile(key)"
                            :disabled="!canDownload"
                            @mousedown.stop
                            @dragstart.stop
                        >
                            <i class="fas fa-download"></i>
                            下载文件
                        </button>
                        <span v-if="downloadStatus" class="download-status">{{ downloadStatus }}</span>
                    </div>
                    <!-- 默认显示文本 -->
                    <span v-else class="param-value">
                        {{ paramConfig.value }}
                        <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
                        <!-- 数据字段的输出连接点 -->
                        <Handle
                            v-if="isDataFieldParam(key)"
                            type="source"
                            :position="Position.Right"
                            :id="`field-${key}`"
                            class="field-output-handle"
                        />
                    </span>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import { Handle, Position } from '@vue-flow/core'
import { computed, inject, ref, onMounted, onUnmounted, nextTick } from 'vue'
import { ipcRenderer } from 'electron'

const validParamTypes = ['text', 'select', 'date', 'daterange', 'multiselect', 'number', 'multiselect-dropdown', 'directory', 'file']

// 定义组件属性
const props = defineProps({
    node: {
        type: Object,
        required: true
    }
})

// 定义发射事件
const emit = defineEmits(['update-node', 'node-click', 'node-context-menu'])

// 注入选中的节点数组
const selectedNodes = inject('selectedNodes', [])

// 存储打开的下拉框状态
const openDropdowns = ref({})
const isEditing = ref(false)
const editingLabel = ref('')
const titleInput = ref(null)
const downloadStatus = ref('')

// 计算是否是多选状态
const isMultiSelected = computed(() => {
    return selectedNodes && selectedNodes.value && selectedNodes.value.length > 1 && 
           selectedNodes.value.find(n => n.id === props.node.id)
})

// 计算在多选中的索引
const selectionIndex = computed(() => {
    if (!selectedNodes || !selectedNodes.value) return 0
    const index = selectedNodes.value.findIndex(n => n.id === props.node.id)
    return index >= 0 ? index + 1 : 0
})

const canDownload = computed(() => {
    // 检查是否有有效的下载路径
    const downloadPath = Object.values(props.node.data.params).find(
        param => param.type === 'directory'
    )?.value
    return downloadPath && downloadPath.trim() !== ''
})

// 检查是否为数据字段参数
const isDataFieldParam = (key) => {
    // 数据源节点的数据字段
    const dataFields = ['close', 'open', 'high', 'low', 'volume']
    return dataFields.includes(key)
}

onMounted(() => {
    document.addEventListener('click', handleClickOutside)
    console.log(`节点 ${props.node.id} (${props.node.data.label}) 的 Handles:`, {
        hasLeftHandle: props.node.data.label !== '数据输入',
        leftHandleId: 'input',
        hasRightHandle: props.node.data.label !== '结果输出' && props.node.data.label !== '数据输入',
        rightHandleId: 'output',
        fieldHandles: Object.keys(props.node.data.params || {})
        .filter(key => isDataFieldParam(key))
        .map(key => `field-${key}`)
    })
})

onUnmounted(() => {
    document.removeEventListener('click', handleClickOutside)
})

// 节点右键点击处理
const onNodeRightClick = (event) => {
    event.preventDefault()
    closeAllDropdowns()
    emit('node-context-menu', { node: props.node, event })
}

const startEditing = () => {
    // 只有 function 类型的节点可以编辑
    if (props.node.data.nodeType === 'function') {
        isEditing.value = true
        editingLabel.value = props.node.data.label
        
        // 下一个tick聚焦输入框
        nextTick(() => {
            if (titleInput.value) {
                titleInput.value.focus()
                titleInput.value.select() // 选中所有文本
            }
        })
    }
}

const saveEditing = () => {
    console.info('saveEditing')
    const label = editingLabel.value.trim()
    if (label !== '') {
        // 发射事件更新节点的label
        emit('update-node', props.node.id, 'label', label)
        props.node.data.label = label
    }
    isEditing.value = false
}
// 取消编辑
const cancelEditing = () => {
    isEditing.value = false
    editingLabel.value = props.node.data.label // 恢复原始值
}
// 处理标题输入框的键盘事件
const onTitleInputKeydown = (event) => {
    // 阻止所有键盘事件冒泡，避免 Vue Flow 处理这些事件
    event.stopPropagation()
    
    // 特别处理退格键，确保只删除文字不删除节点
    if (event.key === 'Backspace' || event.key === 'Delete') {
        event.stopImmediatePropagation()
    }
}
// 点击节点时，如果正在编辑，不触发节点选中
const onNodeClick = (event) => {
    // 如果正在编辑标题，不触发节点选中逻辑
    if (isEditing.value) {
        return
    }
    
    // 关闭所有下拉框
    closeAllDropdowns()
    console.info('selected:', props.node)
    emit('node-click', { node: props.node, event })
}

// 添加点击外部关闭编辑的功能
const handleClickOutside = (event) => {
    if (isEditing.value && !event.target.closest('.node-title')) {
        saveEditing()
    }
}

// 关闭所有下拉框
const closeAllDropdowns = () => {
    openDropdowns.value = {}
}

// 切换下拉框显示状态
const toggleDropdown = (paramKey) => {
    // 关闭其他下拉框
    const newState = { ...openDropdowns.value }
    Object.keys(newState).forEach(key => {
        if (key !== paramKey) {
            newState[key] = false
        }
    })
    
    // 切换当前下拉框状态
    newState[paramKey] = !newState[paramKey]
    openDropdowns.value = newState
}

// 切换选项选择状态
const toggleOptionSelection = (paramKey, option) => {
    const currentValue = props.node.data.params[paramKey]?.value || []
    let newValue
    
    if (currentValue.includes(option)) {
        // 移除选项
        newValue = currentValue.filter(item => item !== option)
    } else {
        // 添加选项
        newValue = [...currentValue, option]
    }
    
    updateParam(paramKey, newValue)
}

// 检查选项是否被选中
const isOptionSelected = (currentValue, option) => {
    if (!currentValue || !Array.isArray(currentValue)) {
        return false
    }
    return currentValue.includes(option)
}

// 获取选中的选项（用于显示）
const getSelectedOptions = (currentValue, allOptions) => {
    if (!currentValue || !Array.isArray(currentValue)) {
        return []
    }
    return allOptions.filter(option => currentValue.includes(option))
}

// 路径选择方法
const selectDirectory = async (paramKey) => {
    try {
        
    } catch (error) {
        console.error('选择路径失败:', error)
        // 用户取消选择或其他错误
    }
}
// 下载文件方法
const downloadFile = async (paramKey) => {
    if (!canDownload.value) {
        downloadStatus.value = '请先选择下载路径'
        setTimeout(() => { downloadStatus.value = '' }, 3000)
        return
    }

    try {
        downloadStatus.value = '下载中...'
        
        // 获取下载路径
        const downloadPath = Object.values(props.node.data.params).find(
            param => param.type === 'directory'
        )?.value

        // 这里调用实际的下载逻辑
        // 假设我们有一个全局的下载方法
        const success = await window.downloadDebugFile?.({
            nodeId: props.node.id,
            downloadPath: downloadPath,
            nodeType: props.node.data.nodeType
        })

        if (success) {
            downloadStatus.value = '下载完成'
            setTimeout(() => { downloadStatus.value = '' }, 2000)
        } else {
            downloadStatus.value = '下载失败'
            setTimeout(() => { downloadStatus.value = '' }, 3000)
        }
    } catch (error) {
        console.error('下载失败:', error)
        downloadStatus.value = '下载错误: ' + error.message
        setTimeout(() => { downloadStatus.value = '' }, 3000)
    }
}
const selectFile = async (paramKey) => {
    try {
        console.info('paramKey', paramKey)
        const filters = getFileFilters(paramKey)
        const result = await ipcRenderer.invoke('select-file', {
            title: `选择${paramKey}文件`,
            filters: filters,
            properties: ['openFile']
        })
        if (result.success) {
            updateParam(paramKey, result.filePath)
        }
    }catch (error) {
        console.error('选择文件失败:', error)
        // 可以添加用户提示
        alert(`选择文件失败: ${error.message}`)
    }
}
// 节点类型映射
const nodeTypeConfig = {
    '数据输入': {
        type: 'dataInput',
        color: '#10b981', // 绿色
        icon: 'fas fa-database',
        hasInput: false,
        hasOutput: false
    },
    '结果输出': {
        type: 'resultOutput',
        color: '#ef4444', // 红色
        icon: 'fas fa-chart-bar',
        hasInput: true,
        hasOutput: false
    },
    'default': {
        icon: 'fas fa-cube',
        hasInput: true,
        hasOutput: true
    }
}

// 计算节点类型
const nodeType = computed(() => {
    return props.node.data.nodeType
})

// 计算节点类名
const nodeClass = computed(() => {
    return `node-type-${nodeType.value}`
})

// 计算头部类名
const headerClass = computed(() => {
    return `header-type-${nodeType.value}`
})

// 计算图标类名
const iconClass = computed(() => {
    return `icon-type-${nodeType.value}`
})

// 计算图标类型
const iconType = computed(() => {
    const label = props.node.data.label
    return nodeTypeConfig[label]?.icon || 'fas fa-cube'
})

// 计算节点颜色
const nodeColor = computed(() => {
    const label = props.node.data.label
    return nodeTypeConfig[label]?.color || '#2962ff'
})

// 检查选项是否被选中
const isOptionChecked = (currentValue, option) => {
    if (!currentValue || !Array.isArray(currentValue)) {
        return false
    }
    return currentValue.includes(option)
}

// 处理复选框变化
const onCheckboxChange = (paramKey, option, isChecked) => {
    const currentValue = props.node.data.params[paramKey]?.value || []
    let newValue
    
    if (isChecked) {
        // 添加选项
        newValue = [...currentValue, option]
    } else {
        // 移除选项
        newValue = currentValue.filter(item => item !== option)
    }
    
    updateParam(paramKey, newValue)
}

// 更新日期范围
const updateDateRange = (paramKey, index, newDateValue) => {
    const currentValue = [...props.node.data.params[paramKey].value]
    currentValue[index] = newDateValue
    
    // 确保结束日期不早于开始日期
    if (index === 0 && currentValue[1] && newDateValue > currentValue[1]) {
        currentValue[1] = newDateValue
    } else if (index === 1 && currentValue[0] && newDateValue < currentValue[0]) {
        currentValue[0] = newDateValue
    }
    
    updateParam(paramKey, currentValue)
}

// 更新参数值
const updateParam = (paramKey, newValue) => {
    emit('update-node', props.node.id, paramKey, newValue)
}

const getFileFilters = (paramKey) => {
  const filterMap = {
    'csv文件': [{ name: 'CSV文件', extensions: ['csv'] }],
    '上传模型': [{ name: '模型文件', extensions: ['onnx'] }],
  }
  return filterMap[paramKey] || [{ name: '所有文件', extensions: ['*'] }]
}
</script>

<style scoped>
/* 多选下拉框样式 */
.multiselect-dropdown-wrapper {
    position: relative;
    width: 100%;
}

.multiselect-dropdown {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 4px 8px;
    cursor: pointer;
    min-height: 28px;
}

.multiselect-dropdown:hover {
    border-color: var(--primary);
}

.selected-options-display {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
    flex: 1;
}

.selected-option-tag {
    background: var(--primary);
    color: white;
    padding: 2px 6px;
    border-radius: 12px;
    font-size: 0.7rem;
    white-space: nowrap;
}

.placeholder {
    color: var(--text-secondary);
    font-size: 0.8rem;
}

.dropdown-arrow {
    transition: transform 0.2s ease;
    margin-left: 8px;
    color: var(--text-secondary);
}

.dropdown-arrow.open {
    transform: rotate(180deg);
}

.dropdown-options {
    position: absolute;
    top: 100%;
    left: 0;
    right: 0;
    background: var(--panel-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    z-index: 1000;
    max-height: 200px;
    overflow-y: auto;
    margin-top: 2px;
}

.dropdown-option {
    display: flex;
    align-items: center;
    padding: 6px 8px;
    cursor: pointer;
    border-bottom: 1px solid var(--border-light);
}

.dropdown-option:last-child {
    border-bottom: none;
}

.dropdown-option:hover {
    background: var(--darker-bg);
}

.dropdown-option input[type="checkbox"] {
    margin-right: 8px;
}

.option-label {
    font-size: 0.8rem;
    color: var(--text);
}

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

/* 节点悬停效果 */
.vue-flow__node-custom:hover {
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
    transform: translateY(-1px);
    transition: all 0.2s ease;
}

/* 选中节点的连接点高亮 */
.vue-flow__node-custom.selected .connection-handle,
.vue-flow__node-custom.multi-selected .connection-handle {
    transform: scale(1.3);
    border-width: 3px;
    border-color: white;
}

.vue-flow__node-custom.selected .left-handle:hover,
.vue-flow__node-custom.multi-selected .left-handle:hover {
    transform: translateY(-50%) scale(1.5);
}

.vue-flow__node-custom.selected .right-handle:hover,
.vue-flow__node-custom.multi-selected .right-handle:hover {
    transform: translateY(-50%) scale(1.5);
}

/* 多选时的统一颜色 */
.vue-flow__node-custom.multi-selected {
    border-color: var(--primary);
}

/* 确保输入控件在选中状态下仍然正常工作 */
.vue-flow__node-custom.selected .param-input,
.vue-flow__node-custom.multi-selected .param-input {
    background: var(--darker-bg);
    border-color: var(--border);
}

.node-header {
    display: flex;
    align-items: center;
    margin-bottom: 8px;
    padding-bottom: 8px;
    border-bottom: 1px solid var(--border);
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
}
/* 路径选择器样式 */
.directory-input {
    width: 100%;
}

.directory-path-display {
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 6px;
}

.path-text {
    flex: 1;
    font-size: 0.8rem;
    color: var(--text);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.directory-select-btn {
    background: var(--primary);
    color: white;
    border: none;
    border-radius: 3px;
    padding: 4px 8px;
    font-size: 0.7rem;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 4px;
    transition: background-color 0.2s ease;
}

.directory-select-btn:hover {
    background: var(--accent);
}

.directory-select-btn:active {
    transform: translateY(1px);
}

/* 下载按钮样式 */
.download-section {
    display: flex;
    align-items: center;
    gap: 8px;
}

.download-btn {
    background: var(--secondary);
    color: white;
    border: none;
    border-radius: 4px;
    padding: 6px 12px;
    font-size: 0.8rem;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 6px;
    transition: all 0.2s ease;
}

.download-btn:hover:not(:disabled) {
    background: var(--accent);
    transform: translateY(-1px);
}

.download-btn:disabled {
    background: var(--border);
    cursor: not-allowed;
    opacity: 0.6;
}

.download-btn:active:not(:disabled) {
    transform: translateY(0);
}

.download-status {
    font-size: 0.7rem;
    color: var(--text-secondary);
    font-style: italic;
}

/* 确保按钮内部的鼠标事件不会触发节点拖动 */
.directory-select-btn,
.download-btn {
    cursor: pointer;
}

.directory-select-btn:active,
.download-btn:active {
    cursor: pointer;
}
/* 文件选择器样式 */
.file-input {
  width: 100%;
}

.file-path-display {
  display: flex;
  align-items: center;
  gap: 8px;
  background: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 6px;
}

.file-select-btn {
  background: var(--primary);
  color: white;
  border: none;
  border-radius: 3px;
  padding: 4px 8px;
  font-size: 0.7rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: background-color 0.2s ease;
}

.file-select-btn:hover {
  background: var(--accent);
}

.file-select-btn:active {
  transform: translateY(1px);
}
/* 图标背景颜色 */
.icon-type-dataInput {
    background-color: #10b981;
}

.icon-type-dataPreprocess {
    background-color: #3b82f6;
}

.icon-type-featureEngineering {
    background-color: #8b5cf6;
}

.icon-type-modelTraining {
    background-color: #f59e0b;
}

.icon-type-resultOutput {
    background-color: #ef4444;
}

.node-title {
    flex: 1;
    font-weight: 600;
    color: var(--text);
    cursor: default;
    min-height: 20px;
    display: flex;
    align-items: center;
}
.node-type-function .node-title:hover::after {
    content: "双击编辑";
    position: absolute;
    top: -20px;
    left: 50%;
    transform: translateX(-50%);
    background: var(--darker-bg);
    color: var(--text-secondary);
    font-size: 0.7rem;
    padding: 2px 6px;
    border-radius: 3px;
    white-space: nowrap;
    opacity: 0;
    animation: fadeIn 0.3s ease forwards;
}
.node-type-function .node-title {
    cursor: pointer;
    position: relative;
}
@keyframes fadeIn {
    to {
        opacity: 1;
    }
}

.title-input {
    width: 100%;
    background: var(--darker-bg);
    border: 1px solid var(--primary);
    border-radius: 4px;
    padding: 2px 6px;
    color: var(--text);
    font-size: 0.9rem;
    font-weight: 600;
    outline: none;
}

.title-input:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.2);
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

.input-with-unit {
    display: flex;
    align-items: center;
    width: 100%;
    position: relative;
}

.param-input {
    width: 100%;
    background: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 4px 8px;
    color: var(--text);
    font-size: 0.8rem;
    flex: 1;
    cursor: text;
}

/* 有单位时的输入框样式 */
.input-with-unit .param-input {
    padding-right: 30px; /* 为单位留出空间 */
}

.param-unit {
    position: absolute;
    right: 8px;
    color: var(--text-secondary);
    font-size: 0.7rem;
    pointer-events: none;
    white-space: nowrap;
}

.param-input {
    width: 100%;
    background: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 4px 8px;
    color: var(--text);
    font-size: 0.8rem;
}

.param-input:focus {
    outline: none;
    border-color: var(--primary);
}

.param-select {
    cursor: pointer;
}

.param-date {
    /* 日期选择器特殊样式 */
}

.param-value {
    font-size: 0.8rem;
    color: var(--text);
    padding: 4px 0;
}

/* 输入控件内部的鼠标事件不应该触发节点拖动 */
.param-input,
.param-select,
.param-date,
.checkbox-item input,
.checkbox-item label {
    cursor: default;
}

/* 当鼠标在输入控件上时，节点不应该显示抓取光标 */
.input-with-unit:hover,
.checkbox-group:hover {
    cursor: default;
}

/* 连接点行样式 */
.node-connection-row {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    pointer-events: none;
}

.connection-handle {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    position: absolute;
    border: 2px solid var(--panel-bg);
    pointer-events: all;
}

.left-handle {
    left: -5px;
    top: 50%;
    transform: translateY(-50%);
}

.right-handle {
    right: -5px;
    top: 50%;
    transform: translateY(-50%);
}

.input-handle {
    background: var(--secondary);
}

.output-handle {
    background: var(--primary);
}

/* 处理连接点的悬停效果 */
.connection-handle:hover {
    transform: scale(1.3);
}

.left-handle:hover {
    transform: translateY(-50%) scale(1.3);
}

.right-handle:hover {
    transform: translateY(-50%) scale(1.3);
}

/* 日期范围选择器样式 */
.date-range-group {
    display: flex;
    flex-direction: column;
    width: 100%;
    gap: 4px;
}

.date-range-inputs {
    display: flex;
    align-items: center;
    gap: 6px;
    width: 100%;
}

.date-range-inputs .param-date {
    flex: 1;
    min-width: 0;
}

.date-range-separator {
    font-size: 0.75rem;
    color: var(--text-secondary);
    flex-shrink: 0;
    white-space: nowrap;
}

/* 确保日期输入框在日期范围组中正确显示 */
.date-range-group .param-unit {
    align-self: flex-end;
    position: static;
    margin-left: 4px;
}

/* 数据字段参数的特殊样式 */
.node-param.data-field-param {
    position: relative;
    padding-right: 3px; /* 为连接点留出空间 */
}

.node-param.data-field-param .param-control {
    justify-content: flex-end; /* 文本靠右 */
    position: relative;
}

.node-param.data-field-param .param-label {
    flex: none; /* 不占用多余空间 */
    margin-right: 8px;
}

.node-param.data-field-param .input-with-unit {
    flex: none; /* 不拉伸 */
    max-width: 120px; /* 限制宽度 */
}

.node-param.data-field-param .param-value {
    text-align: right;
    padding-right: 15px; /* 为连接点留出空间 */
}

/* 字段输出连接点样式 */
.field-output-handle {
    width: 11px;
    height: 11px;
    border-radius: 50%;
    position: absolute;
    right: -8px;
    top: 50%;
    transform: translateY(-10%);
    border: 2px solid var(--text-secondary);
    background: var(--primary);
    pointer-events: all;
    z-index: 10;
}

.field-output-handle:hover {
    transform: translateY(-50%) scale(1.3);
    background: var(--accent);
    right: -8px; /* 悬停状态下也保持靠近边框 */
}

/* 选中状态下字段连接点的样式 */
.vue-flow__node-custom.selected .field-output-handle,
.vue-flow__node-custom.multi-selected .field-output-handle {
    transform: translateY(-50%) scale(1.2);
    border-width: 2px;
    border-color: white;
    right: -8px; /* 选中状态下也保持靠近边框 */
}

/* 确保输入控件在有连接点的情况下仍然正确显示 */
.node-param.data-field-param .input-with-unit .param-input {
    padding-right: 20px; /* 调整单位显示位置 */
}

.node-param.data-field-param .param-unit {
    right: 20px; /* 调整单位位置，避免与连接点重叠 */
}
/* 编辑状态下节点的样式调整 */
.vue-flow__node-custom.editing {
    cursor: default;
}

.vue-flow__node-custom.editing:hover {
    transform: none;
    box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
}
/* 响应式调整：在小屏幕上垂直排列 */
@media (max-width: 200px) {
    .date-range-inputs {
        flex-direction: column;
        align-items: stretch;
    }
    
    .date-range-separator {
        text-align: center;
        margin: 2px 0;
    }
}
</style>