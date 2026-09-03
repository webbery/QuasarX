<template>
  <div class="strategy-models-panel">
    <div v-if="loading && !models" class="loading-row">
      <i class="fas fa-spinner fa-spin"></i> 正在加载模型信息…
    </div>

    <div v-else-if="!models || !models.models || models.models.length === 0" class="empty-row">
      <span v-if="models?.error" class="err">{{ models.error }}</span>
      <span v-else>该策略未绑定 XGBoost 模型节点</span>
    </div>

    <div v-else class="models-table">
      <div class="models-header">
        <div class="col-label">节点</div>
        <div class="col-version">版本号</div>
        <div class="col-name">模型名</div>
        <div class="col-time">生产日期</div>
        <div class="col-features">特征数</div>
        <div class="col-status">状态</div>
      </div>

      <div
        v-for="m in models.models"
        :key="m.node_id"
        class="models-row"
        @mouseenter="hoveredRow = m.node_id"
        @mouseleave="hoveredRow = null"
        @click="togglePopover(m.node_id)"
      >
        <div class="col-label" :title="m.node_id">{{ m.label || '—' }}</div>
        <div class="col-version">
          <span v-if="prodVersion(m)" class="version-chip">{{ prodVersion(m) }}</span>
          <span v-else class="muted">—</span>
        </div>
        <div class="col-name">
          <span v-if="prodDisplayName(m)">{{ prodDisplayName(m) }}</span>
          <span v-else class="muted">—</span>
        </div>
        <div class="col-time">
          <span v-if="prodMeta(m)?.created_at" class="time">{{ formatTime(prodMeta(m)!.created_at) }}</span>
          <span v-else class="muted">未发布</span>
        </div>
        <div class="col-features">
          {{ prodMeta(m)?.n_features ?? '—' }}
        </div>
        <div class="col-status">
          <span v-if="!m.production" class="status-chip status-missing">✗ 缺失</span>
          <span v-else-if="m.is_latest" class="status-chip status-latest">✓ 最新</span>
          <span v-else class="status-chip status-stale">✗ 落后</span>
        </div>

        <!-- 自实现轻量级 popover：hover 或点击展开 -->
        <div
          v-if="hoveredRow === m.node_id || pinnedRow === m.node_id"
          class="popover"
          @click.stop
        >
          <div class="popover-section">
            <div class="popover-title">生产模型</div>
            <table v-if="m.production">
              <tbody>
                <tr><th>路径</th><td>{{ m.production.path }}</td></tr>
                <tr><th>创建时间</th><td>{{ m.production.meta?.created_at }}</td></tr>
                <tr><th>版本号</th><td>{{ m.production.meta?.version ?? '—' }}</td></tr>
                <tr><th>模型名</th><td>{{ m.production.meta?.display_name ?? '—' }}</td></tr>
                <tr v-if="m.production.meta?.description"><th>描述</th><td>{{ m.production.meta.description }}</td></tr>
                <tr><th>训练样本</th>
                  <td>
                    {{ m.production.meta?.n_train ?? '—' }}
                    <span v-if="m.production.meta?.n_val !== undefined"> / {{ m.production.meta.n_val }}</span>
                    <span v-if="m.production.meta?.n_test !== undefined"> / {{ m.production.meta.n_test }}</span>
                  </td>
                </tr>
                <tr v-if="m.production.meta?.test_acc !== undefined"><th>测试准确率</th><td>{{ m.production.meta.test_acc }}</td></tr>
                <tr v-if="m.production.meta?.objective"><th>目标</th><td>{{ m.production.meta.objective }}</td></tr>
                <tr v-if="featuresList(m.production.meta).length">
                  <th>特征 ({{ featuresList(m.production.meta).length }})</th>
                  <td class="features-cell">{{ featuresList(m.production.meta).join(', ') }}</td>
                </tr>
              </tbody>
            </table>
            <div v-else class="muted">未发布（生产目录无对应文件）</div>
          </div>

          <div v-if="m.latest_experiment" class="popover-section">
            <div class="popover-title">最新实验</div>
            <table>
              <tbody>
                <tr><th>路径</th><td>{{ m.latest_experiment.path }}</td></tr>
                <tr><th>创建时间</th><td>{{ m.latest_experiment.meta?.created_at }}</td></tr>
                <tr><th>版本号</th><td>{{ m.latest_experiment.meta?.version ?? '—' }}</td></tr>
                <tr><th>模型名</th><td>{{ m.latest_experiment.meta?.display_name ?? '—' }}</td></tr>
                <tr v-if="m.latest_experiment.meta?.description"><th>描述</th><td>{{ m.latest_experiment.meta.description }}</td></tr>
                <tr><th>训练样本</th>
                  <td>
                    {{ m.latest_experiment.meta?.n_train ?? '—' }}
                    <span v-if="m.latest_experiment.meta?.n_val !== undefined"> / {{ m.latest_experiment.meta.n_val }}</span>
                    <span v-if="m.latest_experiment.meta?.n_test !== undefined"> / {{ m.latest_experiment.meta.n_test }}</span>
                  </td>
                </tr>
                <tr v-if="m.latest_experiment.meta?.test_acc !== undefined"><th>测试准确率</th><td>{{ m.latest_experiment.meta.test_acc }}</td></tr>
              </tbody>
            </table>
            <div v-if="m.is_latest" class="muted small">↑ production 与实验一致，无需重新发布</div>
          </div>

          <div class="popover-actions">
            <button
              v-if="m.production"
              class="btn btn-mini"
              @click.stop="openEditMeta(m)"
            >
              <i class="fas fa-pen"></i> 编辑元数据
            </button>
            <button class="btn btn-mini btn-ghost" @click.stop="pinnedRow = null">
              收起
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- 编辑元数据弹窗（自实现，不引入 Element Plus） -->
    <div v-if="editingItem" class="modal-mask" @click.self="editingItem = null">
      <div class="modal">
        <div class="modal-title">编辑模型元数据</div>
        <div class="modal-subtitle">{{ editingItem.model_file }}</div>
        <div class="modal-form">
          <label>
            版本号
            <input v-model="editingVersion" placeholder="例如 v17" />
          </label>
          <label>
            模型名
            <input v-model="editingDisplayName" placeholder="例如 CTA_v16_baseline" />
          </label>
          <label>
            描述
            <textarea v-model="editingDescription" rows="3" placeholder="可选"></textarea>
          </label>
        </div>
        <div v-if="editError" class="modal-error">{{ editError }}</div>
        <div class="modal-actions">
          <button class="btn btn-ghost" @click="editingItem = null">取消</button>
          <button class="btn btn-primary" :disabled="saving" @click="saveMeta">
            {{ saving ? '保存中…' : '保存' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import axios from 'axios'

const props = defineProps({
  strategyName: { type: String, required: true },
  models: { type: Object, default: null },
  loading: { type: Boolean, default: false },
})

const hoveredRow = ref(null)
const pinnedRow = ref(null)
const editingItem = ref(null)
const editingVersion = ref('')
const editingDisplayName = ref('')
const editingDescription = ref('')
const saving = ref(false)
const editError = ref(null)

function togglePopover(nodeId) {
  pinnedRow.value = pinnedRow.value === nodeId ? null : nodeId
}

function prodMeta(m) {
  return m.production?.meta ?? null
}
function prodVersion(m) {
  return m.production?.meta?.version ?? ''
}
function prodDisplayName(m) {
  return m.production?.meta?.display_name ?? ''
}
function featuresList(meta) {
  return Array.isArray(meta?.features) ? meta.features : []
}
function formatTime(iso) {
  if (!iso) return ''
  // YYYY-MM-DDTHH:MM:SS → YYYY-MM-DD HH:MM
  return iso.slice(0, 16).replace('T', ' ')
}

function openEditMeta(m) {
  editingItem.value = m
  editingVersion.value = m.production?.meta?.version ?? ''
  editingDisplayName.value = m.production?.meta?.display_name ?? ''
  editingDescription.value = m.production?.meta?.description ?? ''
  editError.value = null
}

async function saveMeta() {
  if (!editingItem.value) return
  saving.value = true
  editError.value = null
  try {
    const fields = {}
    if (editingVersion.value) fields.version = editingVersion.value
    if (editingDisplayName.value) fields.display_name = editingDisplayName.value
    fields.description = editingDescription.value

    await axios.post('/v0/ml', {
          action: 'update_meta',
          model_path: editingItem.value.model_file,
          fields,
        })
    ElMessage.success('元数据已保存')
    // 通知父组件刷新缓存
    editingItem.value = null
    // 通过自定义事件让父组件重拉
    window.dispatchEvent(new CustomEvent('strategy-models-changed', {
      detail: { strategyName: props.strategyName },
    }))
  } catch (e) {
    editError.value = e?.response?.data?.message || e?.message || '保存失败'
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.strategy-models-panel {
  position: relative;
  padding: 8px 0;
}

.loading-row, .empty-row {
  padding: 12px;
  text-align: center;
  color: #94a3b8;
  font-size: 13px;
}

.empty-row .err {
  color: #f87171;
}

.models-table {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.models-header, .models-row {
  display: grid;
  grid-template-columns: 1fr 1.2fr 1.5fr 1.6fr 0.8fr 1.2fr;
  gap: 8px;
  padding: 6px 12px;
  align-items: center;
  font-size: 12px;
}

.models-header {
  color: #94a3b8;
  border-bottom: 1px solid rgba(74, 158, 255, 0.15);
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}

.models-row {
  position: relative;
  background: rgba(15, 23, 42, 0.4);
  border-radius: 4px;
  cursor: pointer;
  color: #e2e8f0;
  transition: background 0.12s;
}

.models-row:hover {
  background: rgba(30, 41, 59, 0.7);
}

.col-label { font-weight: 500; }

.col-version .version-chip {
  display: inline-block;
  padding: 1px 8px;
  background: rgba(96, 165, 250, 0.18);
  border: 1px solid rgba(96, 165, 250, 0.35);
  border-radius: 10px;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
  color: #93c5fd;
}

.col-time .time {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}

.col-features {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
  text-align: center;
}

.status-chip {
  display: inline-block;
  padding: 1px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 500;
}

.status-latest {
  background: rgba(74, 222, 128, 0.18);
  color: #4ade80;
  border: 1px solid rgba(74, 222, 128, 0.35);
}

.status-stale {
  background: rgba(251, 191, 36, 0.18);
  color: #fbbf24;
  border: 1px solid rgba(251, 191, 36, 0.35);
}

.status-missing {
  background: rgba(239, 68, 68, 0.15);
  color: #f87171;
  border: 1px solid rgba(239, 68, 68, 0.3);
}

.muted { color: #64748b; }
.muted.small { font-size: 11px; }

/* Popover */
.popover {
  position: absolute;
  top: 100%;
  right: 0;
  margin-top: 6px;
  z-index: 100;
  min-width: 480px;
  max-width: 720px;
  background: rgba(15, 25, 41, 0.98);
  border: 1px solid rgba(74, 158, 255, 0.4);
  border-radius: 8px;
  padding: 14px 18px;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.5);
  font-size: 12px;
}

.popover-section {
  margin-bottom: 12px;
}

.popover-section:last-of-type { margin-bottom: 8px; }

.popover-title {
  color: #60a5fa;
  font-weight: 600;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  margin-bottom: 8px;
}

.popover table {
  width: 100%;
  border-collapse: collapse;
}

.popover th, .popover td {
  text-align: left;
  padding: 3px 8px;
  vertical-align: top;
}

.popover th {
  color: #94a3b8;
  font-weight: 500;
  width: 110px;
  white-space: nowrap;
}

.popover td {
  color: #e2e8f0;
  word-break: break-all;
}

.features-cell {
  max-height: 100px;
  overflow-y: auto;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}

.popover-actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
  margin-top: 6px;
  padding-top: 8px;
  border-top: 1px solid rgba(74, 158, 255, 0.15);
}

.btn {
  padding: 4px 12px;
  border-radius: 4px;
  border: none;
  cursor: pointer;
  font-size: 12px;
  transition: all 0.15s;
}

.btn-mini {
  font-size: 11px;
  padding: 3px 10px;
}

.btn-primary {
  background: rgba(96, 165, 250, 0.25);
  border: 1px solid rgba(96, 165, 250, 0.5);
  color: #93c5fd;
}

.btn-primary:hover:not(:disabled) {
  background: rgba(96, 165, 250, 0.4);
}

.btn-primary:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.btn-ghost {
  background: transparent;
  border: 1px solid rgba(148, 163, 184, 0.3);
  color: #94a3b8;
}

.btn-ghost:hover {
  background: rgba(148, 163, 184, 0.1);
}

/* Edit modal */
.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.modal {
  width: 480px;
  max-width: 90vw;
  background: rgba(15, 25, 41, 0.98);
  border: 1px solid rgba(74, 158, 255, 0.35);
  border-radius: 10px;
  padding: 20px 24px;
  color: #e2e8f0;
}

.modal-title {
  font-size: 15px;
  font-weight: 600;
  margin-bottom: 4px;
}

.modal-subtitle {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
  color: #94a3b8;
  margin-bottom: 14px;
  word-break: break-all;
}

.modal-form {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.modal-form label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
  color: #94a3b8;
}

.modal-form input,
.modal-form textarea {
  background: rgba(15, 23, 42, 0.7);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 4px;
  padding: 6px 10px;
  color: #e2e8f0;
  font-size: 13px;
  font-family: inherit;
  resize: vertical;
}

.modal-form input:focus,
.modal-form textarea:focus {
  outline: none;
  border-color: rgba(96, 165, 250, 0.6);
}

.modal-error {
  margin-top: 10px;
  padding: 6px 10px;
  background: rgba(239, 68, 68, 0.12);
  border: 1px solid rgba(239, 68, 68, 0.3);
  color: #f87171;
  font-size: 12px;
  border-radius: 4px;
}

.modal-actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
  margin-top: 14px;
}
</style>