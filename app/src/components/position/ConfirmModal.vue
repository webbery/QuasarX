<template>
  <div class="modal" v-if="visible" @click.self="handleCancel">
    <div class="modal-content">
      <div class="modal-header">
        <h3>{{ title }}</h3>
        <span class="close" @click="handleCancel">&times;</span>
      </div>
      <div class="modal-body">
        <p>{{ message }}</p>
      </div>
      <div class="modal-footer">
        <button class="btn" @click="handleCancel">取消</button>
        <button class="btn btn-primary" @click="handleConfirm">确定</button>
      </div>
    </div>
  </div>
</template>

<script setup>
const props = defineProps({
  visible: { type: Boolean, required: true },
  title: { type: String, default: '确认' },
  message: { type: String, required: true }
})

const emit = defineEmits(['update:visible', 'confirm'])

const handleCancel = () => {
  emit('update:visible', false)
}

const handleConfirm = () => {
  emit('confirm')
  emit('update:visible', false)
}
</script>

<style scoped>
.modal {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.7);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 1000;
}

.modal-content {
  background-color: var(--panel-bg);
  padding: 20px;
  border-radius: 8px;
  width: 400px;
  max-width: 90%;
  border: 1px solid var(--border);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 15px;
}

.modal-header h3 {
  margin: 0;
  font-size: 1.1rem;
}

.modal-body p {
  margin: 20px 0;
  color: var(--text);
}

.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 20px;
}

.close {
  font-size: 20px;
  cursor: pointer;
  color: var(--text-secondary);
}

.btn {
  padding: 8px 16px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
  border: none;
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  transition: all 0.2s;
}

.btn:hover:not(:disabled) {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  transform: translateY(-1px);
}

.btn-primary {
  background: linear-gradient(90deg, #2962ff, #1e40af);
}
</style>
