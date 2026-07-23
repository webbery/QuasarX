<template>
  <span class="tip">
    <span class="tip-icon" tabindex="0" aria-label="说明">ⓘ</span>
    <span class="tip-content" role="tooltip">{{ content }}</span>
  </span>
</template>

<script setup lang="ts">
defineProps<{
  content: string
}>()
</script>

<style scoped>
.tip {
  position: relative;
  display: inline-flex;
  align-items: center;
  margin-left: 4px;
  cursor: help;
}

.tip-icon {
  color: #6b7280;
  font-size: 12px;
  user-select: none;
  transition: color 0.15s;
}

.tip:hover .tip-icon,
.tip:focus-within .tip-icon {
  color: #4fc3f7;
}

.tip-content {
  position: absolute;
  z-index: 1000;
  bottom: calc(100% + 8px);
  left: 50%;
  transform: translateX(-50%);
  min-width: 180px;
  max-width: 280px;
  padding: 8px 12px;
  background: #1f2937;
  color: #e0e0e0;
  border: 1px solid rgba(74, 85, 104, 0.5);
  border-radius: 6px;
  font-size: 11px;
  line-height: 1.5;
  text-align: left;
  white-space: normal;
  pointer-events: none;
  visibility: hidden;
  opacity: 0;
  transition: opacity 0.15s ease;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
}

.tip-content::after {
  content: '';
  position: absolute;
  top: 100%;
  left: 50%;
  transform: translateX(-50%);
  border: 5px solid transparent;
  border-top-color: #1f2937;
}

.tip:hover .tip-content,
.tip:focus-within .tip-content {
  visibility: visible;
  opacity: 1;
}
</style>
