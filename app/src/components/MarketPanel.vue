<template>
    <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-chart-bar"></i> 市场概览</h3>
          <span>实时</span>
        </div>

        <div class="market-list">
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>沪深300</span>
            </div>
            <div class="market-value positive-change">4,258.76 (+1.2%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-down negative-change"></i>
              <span>中证500</span>
            </div>
            <div class="market-value negative-change">6,325.41 (-0.4%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>创业板指</span>
            </div>
            <div class="market-value positive-change">2,845.33 (+2.1%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>恒生指数</span>
            </div>
            <div class="market-value positive-change">21,458.92 (+0.8%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-down negative-change"></i>
              <span>标普500</span>
            </div>
            <div class="market-value negative-change">4,125.64 (-0.3%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>黄金期货</span>
            </div>
            <div class="market-value positive-change">1,935.40 (+0.6%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>USD/CNY</span>
            </div>
            <div class="market-value " :class="rate_status_class">{{ usd_cny_rate }} ({{ up_rate }}%)</div>
          </div>
        </div>
    </div>
</template>

<script setup >
import { onMounted, ref, onBeforeUnmount } from 'vue'
const axios = require('axios');  

let prev_rate = 0
let usd_cny_rate = ref('0')
let up_rate = ref('0')
let rate_status_class = ref('no-change')
const timer = ref(null)
const isPolling = ref(false);
const interval = ref(10000);

function update_rate() {
  // 更新USD/CNY利率
  const url = "https://www.mxnzp.com/api/exchange_rate/aim?from=USD&to=CNY&app_id=g3xecmg8m8yjmrmy&app_secret=sYj6cRRd497E558UraALJOy6LDo1O61Z";  
  axios.get(url)  
    .then(response => {  
      let value = response['data']['data']['price']
      if (prev_rate == 0) {
        prev_rate = parseFloat(usd_cny_rate.value)
        console.info('prev rate:', prev_rate)
      } else {
        let rate_value = (parseFloat(value) - prev_rate)/prev_rate * 100
        let prev_flag = ''
        if (rate_value > 0) {
          prev_flag = '+'
          rate_status_class.value = 'positive-change'
        }
        else if (rate_value < 0) {
          prev_flag = '-'
          rate_status_class.value = 'negative-change'
        } else {
          rate_status_class.value = 'no-change'
        }
        update_rate.value = prev_flag + rate_value.toFixed(2)
        prev_rate = parseFloat(usd_cny_rate.value)
      }
      console.log(value)
      usd_cny_rate.value = value
    })  
    .catch(error => {  
      console.log(error);  
    });
}

// 开始轮询
const startPolling = () => {
  // 如果已经在轮询中，先停止
  if (isPolling.value) {
    clearInterval(timer.value);
  }
  
  // 立即请求一次
  update_rate();
  
  // 设置定时器
  timer.value = setInterval(() => {
    update_rate();
  }, interval.value);
  
  isPolling.value = true;
};

// 停止轮询
const stopPolling = () => {
  if (timer.value) {
    clearInterval(timer.value);
    timer.value = null;
    isPolling.value = false;
  }
};

onMounted(() => {
  startPolling();
})

onBeforeUnmount(() => {
  stopPolling();
});

async function getSP500() {
  const response = await fetch(url);
  // const text = await response.text();
  // 解析数据格式："var hq_str_gb_gspc=\"标普500,3094.668,...\";"
  // const data = text.split('"')[1].split(',');
  console.log('最新点位：', response);
}
// getSP500();
</script>

<style scoped>
.no-change {
  color: var(--text-secondary);
}
</style>