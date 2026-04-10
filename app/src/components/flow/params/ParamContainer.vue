<!-- src/components/flow/params/ParamContainer.vue -->
<!-- 参数容器组件 - 遍历所有参数并渲染对应控件 -->

<template>
  <div class="node-content">
    <div
      v-for="(paramConfig, key) in params"
      :key="key"
      v-show="paramConfig.visible !== false"
      :class="[
        'node-param',
        { 'data-field-param': isDataFieldParam(key) }
      ]"
    >
      <div class="param-label" v-if="validParamTypes.includes(paramConfig.type)">{{ key }}</div>
      <div class="param-control">
        <!-- 根据参数类型渲染不同控件 -->
        <component
          :is="getParamComponent(paramConfig.type)"
          :param-key="key"
          :param-config="paramConfig"
          :value="paramConfig.value"
          :node-id="nodeId"
          :open-dropdowns="openDropdowns"
          @update="$emit('updateParam', key, $event)"
          @toggle-dropdown="toggleDropdown"
          @open-config-manager="$emit('openConfigManager')"
          @download="$emit('download', key)"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import TextParam from './TextParam.vue'
import NumberParam from './NumberParam.vue'
import SelectParam from './SelectParam.vue'
import MultiSelectParam from './MultiSelectParam.vue'
import MultiSelectDropdownParam from './MultiSelectDropdownParam.vue'
import DateParam from './DateParam.vue'
import DateRangeParam from './DateRangeParam.vue'
import TextareaParam from './TextareaParam.vue'
import DirectoryParam from './DirectoryParam.vue'
import FileParam from './FileParam.vue'
import ConfigSelectParam from './ConfigSelectParam.vue'
import DownloadParam from './DownloadParam.vue'

const props = defineProps<{
  params: Record<string, any>
  nodeId: string
  openDropdowns: Record<string, boolean>
}>()

defineEmits<{
  updateParam: [key: string, value: any]
  toggleDropdown: [key: string]
  openConfigManager: []
  download: [key: string]
}>()

const validParamTypes = ['text', 'select', 'date', 'daterange', 'multiselect', 'number', 'multiselect-dropdown', 'directory', 'file', 'config-select']

const isDataFieldParam = (key: string) => {
  return ['close', 'open', 'high', 'low', 'volume'].includes(key)
}

function toggleDropdown(key: string) {
  // 通过 emit 传递给父组件处理
}

const componentMap: Record<string, any> = {
  'text': TextParam,
  'number': NumberParam,
  'select': SelectParam,
  'multiselect': MultiSelectParam,
  'multiselect-dropdown': MultiSelectDropdownParam,
  'date': DateParam,
  'daterange': DateRangeParam,
  'textarea': TextareaParam,
  'directory': DirectoryParam,
  'file': FileParam,
  'config-select': ConfigSelectParam,
  'download': DownloadParam
}

function getParamComponent(type: string) {
  return componentMap[type] || DefaultParam
}

// 默认参数显示组件
const DefaultParam = {
  props: ['paramKey', 'paramConfig', 'value'],
  template: `<span class="param-value">{{ value }}<span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span></span>`
}
</script>
