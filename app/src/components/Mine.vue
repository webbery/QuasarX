<script setup lang="ts">
import { onMounted, ref, defineOptions } from 'vue'
import axios from 'axios'

defineOptions({
    name: 'MineVue'
})

interface StockItem {
  symbol: string
  name: string
}

const state1 = ref('')
const state2 = ref('')
const stockInfos = ref<StockItem[]>([])

const querySearch = (queryString: string, cb: any) => {
  const results = queryString
    ? stockInfos.value.filter(createFilter(queryString))
    : stockInfos.value
  // call callback function to return suggestions
  cb(results)
}
const createFilter = (queryString: string) => {
  return (restaurant: StockItem) => {
    return (
      restaurant.symbol.indexOf(queryString.toLowerCase()) === 0
    )
  }
}
const loadAll = () => {
  // Get All Stock Info
  // axios.get('https://localhost:11273/v0/stocks/simple')
  //   .then(response => {
  //     console.info('response:', response)
  //     // stockInfos.value = response
  //   })
  //   .catch(error => {
  //     console.error(error)
  //   })
  return [
    
  ]
}

const handleSelect = (item: Record<string, any>) => {
  console.log(item)
}

onMounted(() => {
  loadAll()
})
</script>

<template>
<div class="demo-collapse">
    <el-row :gutter="20">
        <el-col :span="5">
            <div>
                <span>总资金:</span><span>0.0</span>
            </div>
        </el-col>
        <el-col :span="5">
            <div>
                <span>总盈亏:</span><span>0.0</span>
            </div>
        </el-col>
    </el-row>
    <el-calendar ref="calendar">
    <template #header="{ date }">
      <span>收益日历</span>
      <span>{{ date }}</span>
      <!--<el-button-group>
        <el-button size="small" @click="selectDate('prev-year')">
          Previous Year
        </el-button>
        <el-button size="small" @click="selectDate('prev-month')">
          Previous Month
        </el-button>
        <el-button size="small" @click="selectDate('today')">Today</el-button>
        <el-button size="small" @click="selectDate('next-month')">
          Next Month
        </el-button>
        <el-button size="small" @click="selectDate('next-year')">
          Next Year
        </el-button>
      </el-button-group>-->
    </template>
  </el-calendar>
    <el-collapse >
    <el-collapse-item title="A股" name="1">
    <el-row :gutter="20">
        <el-col :span="5">
            <div>
                <span>资金:</span><span>0.0</span>
            </div>
        </el-col>
        <el-col :span="5">
            <div>
                <span>持仓比:</span><span>0.0</span>
            </div>
        </el-col>
        <el-col :span="5">
            <div>
                <span>夏普率:</span><span>0.0%</span>
            </div>
        </el-col>
        <el-col :span="5">
            <div>
                <span>最大回测:</span><span>0.0%</span>
            </div>
        </el-col>
    </el-row>
    <el-table
        style="width: 100%"
    >
        <el-table-column prop="stock" label="股票" width="180" />
        <el-table-column prop="buy_price" label="买入价" width="180" />
        <el-table-column prop="cur_price" label="当前价" />
        <el-table-column prop="sell_rate" label="卖出价" />
        <el-table-column prop="ret_rate" label="收益率" />
    </el-table>
    <el-row :gutter="20">
        <el-col :span="10">
            <el-button type="primary">卖出</el-button>
            <el-button type="primary">撤单</el-button>
        </el-col>
        <el-col :span="10">
            <!--<el-input
              v-model="input3"
              style="max-width: 600px"
              placeholder="Please input"
              class="input-with-select"
            >
              <template #prepend>
                <el-button :icon="Search" />
              </template>
              <template #append>
                <el-select v-model="select" placeholder="代码" style="width: 115px">
                  <el-option label="001003" value="1" />
                  <el-option label="001004" value="2" />
                  <el-option label="001005" value="3" />
                </el-select>
              </template>
            </el-input>
            <el-input
              v-model="input3"
              style="max-width: 600px"
              placeholder="Please input"
              class="input-with-select"
            >
              <template #prepend>
                <el-button :icon="Search" />
              </template>
              <template #append>
                <el-select v-model="select" placeholder="数量" style="width: 115px">
                  <el-option label="Restaurant" value="1" />
                  <el-option label="Order No." value="2" />
                  <el-option label="Tel" value="3" />
                </el-select>
              </template>
            </el-input>
            <el-button type="primary">买入</el-button>
            -->
        </el-col>
    </el-row>
        
    <div>
    回报图
    </div>
</el-collapse-item>
<el-collapse-item title="ETF" name="2">
    <div>
    待研究
    </div>
</el-collapse-item>
<el-collapse-item title="期货" name="2">
    <div>
    待研究
    </div>
</el-collapse-item>
<el-collapse-item title="期权" name="3">
    <div>
    待研究
    </div>
</el-collapse-item>
</el-collapse>
</div>
</template>

<style lang="scss">
.el-row {
  margin-bottom: 20px;
}
.el-row:last-child {
  margin-bottom: 0;
}
.el-col {
  border-radius: 4px;
}

.grid-content {
  border-radius: 4px;
  min-height: 36px;
}
</style>