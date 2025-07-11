<template>
    <div>
        <el-drawer v-model="props.isOpen" title="I am the title" :direction="direction" :before-close="handleClose">
            <el-tabs type="border-card">
                <el-tab-pane label="股票">
                    <el-table :data="stockData" style="width: 100%" lazy >
                        <el-table-column type="selection" width="55" />
                        <el-table-column label="代码" property="symbol" width="90"></el-table-column>
                        <el-table-column label="名称" property="name"  width="120"></el-table-column>
                        <el-table-column label="特点" property="feature" width="100"></el-table-column>
                    </el-table>
                </el-tab-pane>
                <el-tab-pane label="基金"></el-tab-pane>
                <el-tab-pane label="期货"></el-tab-pane>
                <el-tab-pane label="期权"></el-tab-pane>
            </el-tabs>
        </el-drawer>
    </div>
</template>

<script setup >
import { onMounted, ref, onBeforeUnmount, watch } from 'vue'
import axios from 'axios'

const props = defineProps({
  isOpen: {
    type: Boolean,
    default: false,
  },
})

const stockData = ref(null)

watch(
  () => props.isOpen,
)

const emit = defineEmits(['close']);
const handleClose = () => {
  emit('close');
};

onMounted(async () => {
    const reply = await axios.get('/v0/stocks/simple')
    console.info('stocks:', reply)
    stockData.value = reply.data['stocks']
})

// const loadStock = (row, treeNode, resolve) => {
//     setTimeout(() => {
//     resolve([
//       {
//         symbol: '2016-05-01',
//         name: 'wangxiaohu',
//       },
//     ])
//   }, 1000)
// }
</script>