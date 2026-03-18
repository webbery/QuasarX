<template>
  <div class="history-panel" @click.right.prevent @contextmenu.prevent="hideContextMenu">
    <!-- 树形结构区域 -->
    <div class="tree-container">
      <div
        v-for="strategy in strategies"
        :key="strategy.id"
        class="strategy-node"
      >
        <!-- 策略节点（目录） -->
        <div
          class="strategy-header"
          @click="toggleStrategy(strategy.id)"
          @contextmenu.prevent.stop="showContextMenu($event, 'strategy', strategy)"
        >
          <span class="expand-icon">{{ expanded[strategy.id] ? '-' : '+' }}</span>
          <div class="strategy-name-wrapper">
            <span
              v-if="!(editingType === 'strategy' && editingId === strategy.id)"
              class="strategy-name"
            >{{ strategy.name }}
            </span>
            <input
              v-else
              :ref="setEditInputRef"
              v-model="editingValue"
              type="text"
              class="edit-input"
              @blur="saveEdit"
              @keydown.enter="saveEdit"
              @keydown.esc="cancelEdit"
              @click.stop
              @contextmenu.stop
            />
          </div>
          <span class="version-count">({{ versionsByStrategy[strategy.id]?.length || 0 }})</span>
        </div>

        <!-- 版本列表（文件） -->
        <transition name="slide">
          <div v-if="expanded[strategy.id]" class="version-list">
            <div
              v-for="version in versionsByStrategy[strategy.id]"
              :key="version.id"
              class="version-item"
              :class="{ 'selected': selectedVersionId === version.id }"
              @click="loadVersion(version)"
              @contextmenu.prevent.stop="showContextMenu($event, 'version', version)"
            >
              <div class="version-remark-wrapper">
                <span
                  v-if="!(editingType === 'version' && editingId === version.id)"
                  class="version-remark"
                  :title="version.remark"
                >{{ version.remark }}</span>
                <input
                  v-else
                  :ref="setEditInputRef"
                  v-model="editingValue"
                  type="text"
                  class="edit-input"
                  @blur="saveEdit"
                  @keydown.enter="saveEdit"
                  @keydown.esc="cancelEdit"
                  @click.stop
                  @contextmenu.stop
                />
              </div>
            </div>
          </div>
        </transition>
      </div>
    </div>

    <!-- 新建策略按钮 -->
    <div class="new-strategy-btn" @click="showNewStrategyDialog">
      <i class="fas fa-plus"></i>
      <span>新建策略</span>
    </div>

    <!-- 自定义右键菜单 -->
    <div
      v-if="contextMenu.visible"
      class="context-menu"
      :style="{ top: contextMenu.y + 'px', left: contextMenu.x + 'px' }"
      @click.stop
    >
      <div
        v-for="item in menuItems"
        :key="item.label"
        class="menu-item"
        @click="handleMenuItemClick(item.action)"
      >
        {{ item.label }}
      </div>
    </div>

    <!-- 新建策略对话框 -->
    <PromptDialog ref="promptDialogRef" />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { useHistoryStore } from '@/stores/history'
import PromptDialog from './PromptDialog.vue'
import { message } from '@/tool'
import { storeToRefs } from 'pinia'

// ---------- Pinia Store ----------
const store = useHistoryStore()
const { strategies, versions } = storeToRefs(store)
const { addStrategy, updateStrategyName, removeStrategy, updateVersionRemark, removeVersion } = store

const editingId = ref<string | null>(null)
const editingType = ref<'strategy' | 'version' | null>(null)
const editingValue = ref('')
let currentInput: HTMLInputElement | null = null  // 用于聚焦

// 新建策略对话框状态
const showDialog = ref(false)
const newStrategyName = ref('')
const promptDialogRef = ref<InstanceType<typeof PromptDialog>>()

// 按策略 ID 分组的版本
const versionsByStrategy = computed(() => {
  const map: Record<string, typeof versions.value> = {}
  versions.value.filter(v => v).forEach(v => {
    if (!map[v.strategyId]) map[v.strategyId] = []
    map[v.strategyId].push(v)
  })
  return map
})

// 格式化日期（与默认备注一致）
const formatDate = (isoString: string): string => {
  const date = new Date(isoString)
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`
}

// ---------- 展开/收起 ----------
const expanded = reactive<Record<string, boolean>>({})
const toggleStrategy = (id: string) => {
  expanded[id] = !expanded[id]
}

// ---------- 右键菜单 ----------
const contextMenu = reactive({
  visible: false,
  x: 0,
  y: 0,
  type: '' as 'strategy' | 'version',
  data: null as any
})

// 根据右键节点类型生成菜单项
const menuItems = computed(() => {
  if (contextMenu.type === 'strategy') {
    return [
      { label: '编辑', action: 'editStrategyName' },
      { label: '新建版本', action: 'createNewVersion' },
      { label: '删除', action: 'deleteAllVersions' }
    ]
  } else if (contextMenu.type === 'version') {
    return [
      { label: '编辑', action: 'editRemark' },
      { label: '删除', action: 'deleteVersion' }
    ]
  }
  return []
})

const setEditInputRef = (el: any) => {
  if (el && (editingType.value === 'strategy' || editingType.value === 'version')) {
    currentInput = el
  }
}

// 启动内联编辑
const startEditing = (type: 'strategy' | 'version', id: string, initialValue: string) => {
  editingType.value = type
  editingId.value = id
  editingValue.value = initialValue
  nextTick(() => {
    if (currentInput) {
      currentInput.focus()
    }
  })
}

// 保存编辑
const saveEdit = async () => {
  if (!editingType.value || !editingId.value) return

  const newValue = editingValue.value.trim()
  const type = editingType.value
  const id = editingId.value

  if (type === 'strategy') {
    if (newValue === '') {
      cancelEdit()  // 空字符串取消编辑（不做修改）
      return
    }
    await updateStrategyName(id, newValue)
  } else if (type === 'version') {
    let finalRemark = newValue
    if (finalRemark === '') {
      // 空备注重置为保存时间
      const version = versions.value.find(v => v.id === id)
      if (version) {
        finalRemark = formatDate(version.saveTime)
      } else {
        cancelEdit()
        return
      }
    }
    await updateVersionRemark(id, finalRemark)
  }

  clearEdit()
}

// 取消编辑
const cancelEdit = () => {
  clearEdit()
}

const clearEdit = () => {
  editingType.value = null
  editingId.value = null
  editingValue.value = ''
  currentInput = null
}

// 显示菜单
const showContextMenu = (e: MouseEvent, type: 'strategy' | 'version', data: any) => {
  e.preventDefault()
  contextMenu.visible = true
  contextMenu.x = e.clientX
  contextMenu.y = e.clientY
  contextMenu.type = type
  contextMenu.data = data
}

// 隐藏菜单（点击其他地方）
const hideContextMenu = () => {
  contextMenu.visible = false
}

// 点击菜单项处理
const handleMenuItemClick = async (action: string) => {
  if (!contextMenu.data) return

  if (action === 'editStrategyName') {
    startEditing('strategy', contextMenu.data.id, contextMenu.data.name)
  } else if (action === 'createNewVersion') {
    // 创建新版本逻辑（通知父组件）
    emit('createNewVersion', contextMenu.data.id)
    hideContextMenu()
    return
  } else if (action === 'deleteAllVersions') {
    if (confirm(`确定要删除策略 "${contextMenu.data.name}" 及其所有历史版本吗？`)) {
      await removeStrategy(contextMenu.data.id)
    }
  } else if (action === 'editRemark') {
    startEditing('version', contextMenu.data.id, contextMenu.data.remark)
  } else if (action === 'deleteVersion') {
    if (confirm(`确定要删除 ${formatDate(contextMenu.data.saveTime)} 的版本吗？`)) {
      await removeVersion(contextMenu.data.id)
    }
  }

  hideContextMenu()
}

// 全局点击关闭菜单
const handleGlobalClick = () => {
  hideContextMenu()
}

// 显示新建策略对话框
const showNewStrategyDialog = async () => {
  const name = await promptDialogRef.value?.show({
    title: '新建策略',
    placeholder: '请输入策略名称'
  })
  if (name && name.trim()) {
    const strategyId = addStrategy(name.trim())
    expanded[strategyId] = true
    message.success(`已创建策略 "${name}"`)
  }
}

// 关闭对话框
const closeDialog = () => {
  showDialog.value = false
}

// 创建新策略
const createStrategy = () => {
  const name = newStrategyName.value.trim()
  if (!name) {
    message.error('请输入策略名称')
    return
  }

  const strategyId = addStrategy(name)
  expanded[strategyId] = true
  closeDialog()
  message.success(`已创建策略 "${name}"`)
}

onMounted(() => {
  window.addEventListener('click', handleGlobalClick)
})
onUnmounted(() => {
  window.removeEventListener('click', handleGlobalClick)
})

// ---------- 双击版本载入 ----------
const selectedVersionId = ref<string | null>(null)
const currentStrategyId = ref<string | null>(null)
const emit = defineEmits<{
  (e: 'load', version: any): void
  (e: 'loadVersion', versionId: string): void
  (e: 'createNewVersion', strategyId: string): void
}>()

const loadVersion = (version: any) => {
  selectedVersionId.value = version.id
  emit('load', version)
  emit('loadVersion', version.id)
}
</script>

<style scoped>
.history-panel {
  height: 100%;
  background-color: var(--darker-bg);
  border-left: 1px solid var(--border);
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
  color: var(--text);
  overflow: auto;
  position: relative;
}

.tree-container {
  padding: 16px 12px;
}

.strategy-node {
  margin-bottom: 8px;
}

.strategy-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  background-color: var(--darker-bg);
  border-radius: 8px;
  cursor: pointer;
  user-select: none;
  color: var(--text);
}

.strategy-header:hover {
  background-color: var(--border);
}

.expand-icon {
  font-size: 1.2rem;
  color: var(--text);
}

.strategy-name {
  font-weight: 600;
  font-size: 1rem;
  flex: 1;
  color: var(--text);
}

.version-count {
  font-size: 0.85rem;
  color: var(--text-secondary);
}

.version-list {
  margin-left: 24px;
  margin-top: 4px;
  border-left: 2px dashed var(--border);
  padding-left: 12px;
}

.version-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  background-color: var(--darker-bg);
  cursor: pointer;
  color: var(--text);
}

.version-item:hover {
  background-color: var(--border);
}

.version-item.selected {
  background-color: rgba(41, 98, 255, 0.2);
  border-color: var(--primary);
}

.version-time {
  font-size: 0.85rem;
  color: var(--text-secondary);
  min-width: 130px;
  white-space: nowrap;
}

.version-remark {
  font-size: 0.95rem;
  color: var(--text);
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.empty-versions {
  padding: 8px 12px;
  color: var(--text-secondary);
  font-style: italic;
  font-size: 0.9rem;
}

/* 右键菜单 */
.context-menu {
  position: fixed;
  background-color: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.5);
  padding: 4px 0;
  z-index: 1000;
  min-width: 160px;
  color: var(--text);
}

.menu-item {
  padding: 8px 16px;
  cursor: pointer;
  font-size: 0.9rem;
  color: var(--text);
  transition: background-color 0.2s;
}

.menu-item:hover {
  background-color: var(--border);
}

/* 内联输入框样式 */
.strategy-name-wrapper,
.version-remark-wrapper {
  flex: 1;
  display: flex;
  align-items: center;
}

.edit-input {
  background: var(--darker-bg);
  border: 1px solid var(--primary);
  border-radius: 4px;
  color: var(--text);
  font-size: 0.95rem;
  padding: 2px 6px;
  width: 100%;
  outline: none;
  font-family: inherit;
}

/* 确保版本项内的文本不换行 */
.version-remark {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

/* 折叠动画 */
.slide-enter-active,
.slide-leave-active {
  transition: all 0.2s ease;
  overflow: hidden;
  max-height: 300px;
}
.slide-enter-from,
.slide-leave-to {
  opacity: 0;
  max-height: 0;
  margin-top: 0;
  margin-bottom: 0;
  padding-top: 0;
  padding-bottom: 0;
}

/* 新建策略按钮 */
.new-strategy-btn {
  position: absolute;
  bottom: 20px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border-radius: 25px;
  cursor: pointer;
  font-size: 0.9rem;
  font-weight: 500;
  box-shadow: 0 4px 12px rgba(37, 99, 235, 0.3);
  transition: all 0.3s ease;
}

.new-strategy-btn:hover {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  box-shadow: 0 6px 16px rgba(37, 99, 235, 0.4);
  transform: translateX(-50%) translateY(-2px);
}

.new-strategy-btn i {
  font-size: 1rem;
}

/* 对话框样式 */
.dialog-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 2000;
  backdrop-filter: blur(4px);
}

.dialog {
  background: var(--panel-bg);
  border-radius: 12px;
  padding: 24px;
  min-width: 320px;
  max-width: 400px;
  box-shadow: 0 12px 48px rgba(0, 0, 0, 0.4);
  border: 1px solid var(--border);
}

.dialog-title {
  margin: 0 0 20px 0;
  font-size: 1.2rem;
  font-weight: 600;
  color: var(--text);
  text-align: center;
}

.dialog-input {
  width: 100%;
  padding: 12px 16px;
  border: 1px solid var(--border);
  border-radius: 8px;
  background: var(--darker-bg);
  color: var(--text);
  font-size: 1rem;
  outline: none;
  transition: border-color 0.2s;
  box-sizing: border-box;
}

.dialog-input:focus {
  border-color: var(--primary);
}

.dialog-input::placeholder {
  color: var(--text-secondary);
}

.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 20px;
}

.dialog-btn {
  padding: 8px 20px;
  border-radius: 8px;
  font-size: 0.95rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  border: none;
}

.dialog-btn.cancel {
  background: var(--darker-bg);
  color: var(--text);
  border: 1px solid var(--border);
}

.dialog-btn.cancel:hover {
  background: var(--border);
}

.dialog-btn.confirm {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
}

.dialog-btn.confirm:hover {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  box-shadow: 0 4px 12px rgba(37, 99, 235, 0.3);
}
</style>
