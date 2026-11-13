<template>
  <div class="agent-action-artifact">
    <div v-if="payload.context" class="action-context">{{ payload.context }}</div>
    <div v-else class="action-context">以下操作需要你确认后才会执行。</div>
  </div>
</template>

<script setup lang="ts">
import type { ActionPayload } from '@/lib/agent/artifacts'

defineProps<{ payload: ActionPayload }>()
</script>

<style scoped>
.agent-action-artifact {
  padding: 10px 13px;
}

.action-context {
  font-size: 12.5px;
  line-height: 1.65;
  color: #a0aec0;
}
</style>
