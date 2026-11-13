<template>
  <div class="agent-fallback">
    <div class="fallback-hint">
      该产物类型「{{ kind }}」尚未注册渲染器，以下是原始数据：
    </div>
    <pre class="fallback-json">{{ raw }}</pre>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { describeUnregisteredArtifact } from '@/lib/agent/artifactRegistry'
import type { ArtifactEnvelope } from '@/lib/agent/artifacts'

const props = defineProps<{ payload: unknown; kind: string; artifact?: ArtifactEnvelope }>()

const raw = computed(() => {
  if (props.artifact) return describeUnregisteredArtifact(props.artifact)
  try {
    return JSON.stringify(props.payload, null, 2)
  } catch {
    return String(props.payload)
  }
})
</script>

<style scoped>
.agent-fallback {
  padding: 10px 13px;
}

.fallback-hint {
  font-size: 11.5px;
  color: #6f7b8a;
  margin-bottom: 7px;
}

.fallback-json {
  margin: 0;
  padding: 10px 12px;
  background: rgba(0, 0, 0, 0.28);
  border-radius: 8px;
  font-family: "SF Mono", Menlo, Consolas, monospace;
  font-size: 11px;
  line-height: 1.6;
  color: #a0aec0;
  max-height: 260px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-all;
}
</style>
