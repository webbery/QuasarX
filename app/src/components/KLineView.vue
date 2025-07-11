<template>
    <div >
        <div ref="chartContainer" style="width: 100%;height: 400px;"></div>
        <button>K</button><button>21VaR</button>
    </div>
</template>

<script setup>
import { onMounted, ref, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps({
  chartData: {
    type: Object,
    default: () => ({}),
  },
  ops: {
    type: Array,
    default: []
  }
});

const chartContainer = ref(null)
let charInstance = null

const renderChart = () => {
    if (!charInstance || !props.chartData)
        return

    const option = {
        tooltip: {
          trigger: 'axis',
          axisPointer: {
            type: 'cross',
          },
        },
        dataZoom: [
          {
            type: 'slider',
            show: true,
            xAxisIndex: [0],
            start: 1,
            end: 35
          },
          {
            type: 'slider',
            show: true,
            yAxisIndex: [0],
            left: '93%',
            start: 29,
            end: 36
          },
          {
            type: 'inside',
            xAxisIndex: [0],
            start: 1,
            end: 35
          },
          {
            type: 'inside',
            yAxisIndex: [0],
            start: 29,
            end: 36
          }
        ],
        xAxis: {
          type: 'category',
          data: props.chartData.map((item) => new Date(item.datetime * 1000).toLocaleDateString()),
        },
        yAxis: {
          scale: true,
        },
        series: [
            {
                type: 'candlestick',
                data: props.chartData.map((item) => [item.open, item.close, item.low, item.high]),
                markPoint: {
                    data: props.ops.map((item) => {
                        return {
                            value: (item.long?'B':'S'),
                            coord: [new Date(item.datetime * 1000).toLocaleDateString(), item.price]
                        }
                    }),
                    symbol: 'pin',
                    symbolSize: 40,
                    label: { show: true, color: 'black', fontSize: 12 },
                    itemStyle: { color: '#FF4500' }
                }
            },
        ],
      };
      charInstance.setOption(option);
}

watch(
  () => props.chartData,
  () => {
    renderChart(); // 数据变化时自动更新图表
  },
  { deep: true }
);

onMounted(() => {
    charInstance = echarts.init(chartContainer.value)
    renderChart()
})

onBeforeUnmount(() => {
    if (chartInstance) chartInstance.dispose(); // 销毁图表实例
});

// 暴露方法给父组件
defineExpose({
    renderChart,
});
</script>

